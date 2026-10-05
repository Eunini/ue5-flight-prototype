#include "UI/FlightHUD.h"

#include "Aircraft/FlightPawn.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/CabinInteractionComponent.h"
#include "Systems/AircraftSystemsComponent.h"

namespace
{
	constexpr float LineHeight = 18.f;

	FLinearColor SeverityColor(EDiagnosticSeverity Severity)
	{
		switch (Severity)
		{
		case EDiagnosticSeverity::Advisory: return FLinearColor(0.4f, 0.8f, 1.f);
		case EDiagnosticSeverity::Warning:  return FLinearColor(1.f, 0.75f, 0.1f);
		case EDiagnosticSeverity::Fault:    return FLinearColor(1.f, 0.25f, 0.2f);
		default:                            return FLinearColor(0.6f, 1.f, 0.6f);
		}
	}
}

void AFlightHUD::Line(const FString& Text, float X, float& Y, const FLinearColor& Color)
{
	DrawText(Text, Color, X, Y, GEngine->GetSmallFont());
	Y += LineHeight;
}

void AFlightHUD::DrawHUD()
{
	Super::DrawHUD();

	const AFlightPawn* Pawn = GetOwningPlayerController() ? Cast<AFlightPawn>(GetOwningPlayerController()->GetPawn()) : nullptr;
	if (!Pawn || !Canvas)
	{
		return;
	}

 DrawRect(FLinearColor(.025f,.04f,.055f,.86f),0,0,Canvas->ClipX,124.f);
 DrawText(TEXT("AERONAUT / ALPINE FLIGHT"),FLinearColor(.97f,.5f,.16f),20,18,GEngine->GetLargeFont());
	float Y = 50.f;
	DrawTelemetry(*Pawn, 20.f, Y);

	if (Pawn->IsDiagnosticsOpen())
	{
		float DiagY = 20.f;
		DrawDiagnostics(*Pawn, Canvas->ClipX - 360.f, DiagY);
	}

	if (Pawn->IsInCabinView())
	{
		const float CX = Canvas->ClipX * 0.5f;
		const float CY = Canvas->ClipY * 0.5f;
		DrawRect(FLinearColor::White, CX - 2.f, CY - 2.f, 4.f, 4.f);
		if (const UCabinInteractionComponent* Interaction = Pawn->GetInteraction())
		{
			const FText Prompt = Interaction->GetCurrentPrompt();
			if (!Prompt.IsEmpty())
			{
				float PromptY = CY + 20.f;
				Line(Prompt.ToString(), CX - 90.f, PromptY, FLinearColor::Yellow);
			}
		}
	}

 DrawRect(FLinearColor(.025f,.04f,.055f,.86f),0,Canvas->ClipY-84.f,Canvas->ClipX,84.f);
	float HelpY = Canvas->ClipY - 3 * LineHeight - 10.f;
	const FLinearColor Help(0.8f, 0.8f, 0.8f);
	Line(TEXT("W/S pitch  A/D roll  Q/E rudder  Shift/Ctrl throttle  B brake"), 20.f, HelpY, Help);
	Line(TEXT("V cabin view  F use switch  Tab diagnostics"), 20.f, HelpY, Help);
	Line(TEXT("Start: Battery ON, Fuel pump ON, Magnetos ON, then Starter"), 20.f, HelpY, Help);
}

void AFlightHUD::DrawTelemetry(const AFlightPawn& Pawn, float X, float& Y)
{
	const FlightCore::Telemetry& T = Pawn.GetTelemetry();
	constexpr double MsToKnots = 1.943844;
	Line(FString::Printf(TEXT("IAS %5.0f kt   ALT %6.0f m   VS %+5.1f m/s"), T.Airspeed * MsToKnots, Pawn.GetAltitudeMetres(), T.VerticalSpeed), X, Y);
	Line(FString::Printf(TEXT("THR %3.0f%%   AoA %5.1f deg   CL %4.2f"), Pawn.GetThrottleSetting() * 100.f, T.AlphaDeg, T.LiftCoefficient), X, Y);
	if (T.bStalled)
	{
		Line(TEXT("STALL"), X, Y, FLinearColor::Red);
	}
	if (const UAircraftSystemsComponent* Systems = Pawn.GetSystems())
	{
		if (Systems->GetEngineState() != EEngineState::Running)
		{
			Line(TEXT("ENGINE NOT RUNNING (Tab for diagnostics)"), X, Y, FLinearColor(1.f, 0.75f, 0.1f));
		}
	}
	if (Pawn.GetLocalRole() == ROLE_AutonomousProxy)
	{
		Line(FString::Printf(TEXT("net: %d unacked inputs, last correction %.1f cm"), Pawn.GetUnacknowledgedInputCount(), Pawn.GetLastCorrectionCm()),
			X, Y, FLinearColor(0.6f, 0.6f, 0.6f));
	}
}

void AFlightHUD::DrawDiagnostics(const AFlightPawn& Pawn, float X, float& Y)
{
	const UAircraftSystemsComponent* Systems = Pawn.GetSystems();
	if (!Systems)
	{
		return;
	}
	const TArray<FDiagnosticEntry> Rows = Systems->BuildDiagnostics();
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X - 10.f, Y - 6.f, 350.f, (Rows.Num() + 1) * LineHeight + 12.f);
	Line(TEXT("SYSTEMS DIAGNOSTIC"), X, Y);
	for (const FDiagnosticEntry& Row : Rows)
	{
		Line(FString::Printf(TEXT("%-16s %s"), *Row.Label.ToString(), *Row.Value.ToString()), X, Y, SeverityColor(Row.Severity));
	}
}
