// Asset-free HUD: flight telemetry, interaction prompt, cabin diagnostic menu and net stats.
// A UMG widget would replace this once UI art exists; the data calls stay the same.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FlightHUD.generated.h"

UCLASS()
class FLIGHTPROTO_API AFlightHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawTelemetry(const class AFlightPawn& Pawn, float X, float& Y);
	void DrawDiagnostics(const class AFlightPawn& Pawn, float X, float& Y);
	void Line(const FString& Text, float X, float& Y, const FLinearColor& Color = FLinearColor::White);
};
