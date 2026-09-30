#include "Systems/AircraftSystemsComponent.h"

#include "Net/UnrealNetwork.h"

#define LOCTEXT_NAMESPACE "AircraftSystems"

namespace
{
	constexpr float IdleRpm = 700.f;
	constexpr float MaxRpm = 2700.f;
	constexpr float CrankRpm = 250.f;
}

UAircraftSystemsComponent::UAircraftSystemsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UAircraftSystemsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UAircraftSystemsComponent, SwitchBits);
	DOREPLIFETIME(UAircraftSystemsComponent, EngineState);
	DOREPLIFETIME(UAircraftSystemsComponent, LastFault);
	DOREPLIFETIME(UAircraftSystemsComponent, FuelLitres);
	DOREPLIFETIME(UAircraftSystemsComponent, BatteryCharge);
	DOREPLIFETIME(UAircraftSystemsComponent, EngineRpm);
}

void UAircraftSystemsComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner()->HasAuthority())
	{
		FuelLitres = FuelCapacityLitres * 0.8f;
	}
	else
	{
		// Only the server simulates systems; clients just read replicated state.
		SetComponentTickEnabled(false);
	}
}

void UAircraftSystemsComponent::OperateSwitch(ECabinSwitch Switch)
{
	check(GetOwner()->HasAuthority());

	if (Switch == ECabinSwitch::Starter)
	{
		if (EngineState == EEngineState::Off || EngineState == EEngineState::Failed)
		{
			const EEngineFault Fault = IsSwitchOn(ECabinSwitch::Battery) && BatteryCharge > 0.05f
				? EEngineFault::None
				: EEngineFault::NoElectricalPower;
			if (Fault == EEngineFault::None)
			{
				CrankTimer = 0.f;
				SetEngineState(EEngineState::Cranking);
			}
			else
			{
				SetEngineState(EEngineState::Failed, Fault);
			}
		}
		return;
	}

	SwitchBits ^= SwitchMask(Switch);
	Notify();
}

EEngineFault UAircraftSystemsComponent::CheckRunConditions() const
{
	if (FuelLitres <= 0.f)
	{
		return EEngineFault::FuelExhausted;
	}
	if (!IsSwitchOn(ECabinSwitch::FuelPump))
	{
		return EEngineFault::NoFuelPressure;
	}
	if (!IsSwitchOn(ECabinSwitch::Magnetos))
	{
		return EEngineFault::NoIgnition;
	}
	return EEngineFault::None;
}

void UAircraftSystemsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	switch (EngineState)
	{
	case EEngineState::Cranking:
	{
		CrankTimer += DeltaTime;
		BatteryCharge = FMath::Max(0.f, BatteryCharge - StarterDrainPerSecond * DeltaTime);
		EngineRpm = CrankRpm;
		if (BatteryCharge <= 0.f || !IsSwitchOn(ECabinSwitch::Battery))
		{
			SetEngineState(EEngineState::Failed, BatteryCharge <= 0.f ? EEngineFault::BatteryFlat : EEngineFault::NoElectricalPower);
		}
		else if (CrankTimer >= CrankDurationSeconds)
		{
			const EEngineFault Fault = CheckRunConditions();
			SetEngineState(Fault == EEngineFault::None ? EEngineState::Running : EEngineState::Failed, Fault);
		}
		break;
	}
	case EEngineState::Running:
	{
		const float TargetRpm = FMath::Lerp(IdleRpm, MaxRpm, Throttle);
		EngineRpm = FMath::FInterpTo(EngineRpm, TargetRpm, DeltaTime, 2.f);
		const float BurnPerSecond = FuelBurnLitresPerHourAtFullThrottle / 3600.f * FMath::Lerp(0.25f, 1.f, Throttle);
		FuelLitres = FMath::Max(0.f, FuelLitres - BurnPerSecond * DeltaTime);
		if (IsSwitchOn(ECabinSwitch::Battery))
		{
			BatteryCharge = FMath::Min(1.f, BatteryCharge + 0.01f * DeltaTime); // alternator
		}
		const EEngineFault Fault = CheckRunConditions();
		if (Fault != EEngineFault::None)
		{
			SetEngineState(EEngineState::Failed, Fault);
		}
		break;
	}
	default:
		EngineRpm = FMath::FInterpTo(EngineRpm, 0.f, DeltaTime, 1.5f);
		if (IsSwitchOn(ECabinSwitch::NavLights) && IsSwitchOn(ECabinSwitch::Battery))
		{
			BatteryCharge = FMath::Max(0.f, BatteryCharge - 0.002f * DeltaTime);
		}
		break;
	}
}

void UAircraftSystemsComponent::SetEngineState(EEngineState NewState, EEngineFault Fault)
{
	EngineState = NewState;
	LastFault = Fault;
	Notify();
}

void UAircraftSystemsComponent::Notify()
{
	// Listen-server hosts do not receive OnRep calls, so broadcast directly as well.
	OnSystemsChanged.Broadcast();
}

void UAircraftSystemsComponent::OnRep_Systems()
{
	OnSystemsChanged.Broadcast();
}

FText UAircraftSystemsComponent::GetSwitchLabel(ECabinSwitch Switch) const
{
	switch (Switch)
	{
	case ECabinSwitch::Battery:   return LOCTEXT("Battery", "Master battery");
	case ECabinSwitch::FuelPump:  return LOCTEXT("FuelPump", "Fuel pump");
	case ECabinSwitch::Magnetos:  return LOCTEXT("Magnetos", "Magnetos");
	case ECabinSwitch::Starter:   return LOCTEXT("Starter", "Starter");
	case ECabinSwitch::NavLights: return LOCTEXT("NavLights", "Nav lights");
	default:                      return FText::GetEmpty();
	}
}

TArray<FDiagnosticEntry> UAircraftSystemsComponent::BuildDiagnostics() const
{
	TArray<FDiagnosticEntry> Rows;
	auto Add = [&Rows](FText Label, FText Value, EDiagnosticSeverity Severity)
	{
		FDiagnosticEntry& Row = Rows.AddDefaulted_GetRef();
		Row.Label = MoveTemp(Label);
		Row.Value = MoveTemp(Value);
		Row.Severity = Severity;
	};

	static const FText StateText[] = {
		LOCTEXT("EngineOff", "OFF"), LOCTEXT("EngineCranking", "CRANKING"),
		LOCTEXT("EngineRunning", "RUNNING"), LOCTEXT("EngineFailed", "FAILED")};
	Add(LOCTEXT("Engine", "Engine"), StateText[static_cast<uint8>(EngineState)],
		EngineState == EEngineState::Failed ? EDiagnosticSeverity::Fault : EDiagnosticSeverity::Normal);

	Add(LOCTEXT("Rpm", "RPM"), FText::AsNumber(FMath::RoundToInt(EngineRpm)), EDiagnosticSeverity::Normal);

	const float FuelFraction = FuelCapacityLitres > 0.f ? FuelLitres / FuelCapacityLitres : 0.f;
	Add(LOCTEXT("Fuel", "Fuel"), FText::Format(LOCTEXT("FuelValue", "{0} L"), FText::AsNumber(FMath::RoundToInt(FuelLitres))),
		FuelFraction < 0.1f ? EDiagnosticSeverity::Warning : EDiagnosticSeverity::Normal);

	Add(LOCTEXT("BatteryCharge", "Battery"), FText::AsPercent(BatteryCharge),
		BatteryCharge < 0.2f ? EDiagnosticSeverity::Warning : EDiagnosticSeverity::Normal);

	for (uint8 i = 0; i < static_cast<uint8>(ECabinSwitch::Count); ++i)
	{
		const ECabinSwitch Switch = static_cast<ECabinSwitch>(i);
		if (Switch == ECabinSwitch::Starter)
		{
			continue;
		}
		Add(GetSwitchLabel(Switch), IsSwitchOn(Switch) ? LOCTEXT("On", "ON") : LOCTEXT("Off", "OFF"), EDiagnosticSeverity::Normal);
	}

	if (LastFault != EEngineFault::None)
	{
		static const FText FaultText[] = {
			FText::GetEmpty(),
			LOCTEXT("FaultPower", "No electrical power: master battery off"),
			LOCTEXT("FaultFuel", "No fuel pressure: fuel pump off"),
			LOCTEXT("FaultIgnition", "No ignition: magnetos off"),
			LOCTEXT("FaultExhausted", "Fuel exhausted"),
			LOCTEXT("FaultBattery", "Battery flat")};
		Add(LOCTEXT("Fault", "Last fault"), FaultText[static_cast<uint8>(LastFault)], EDiagnosticSeverity::Fault);
	}
	return Rows;
}

#undef LOCTEXT_NAMESPACE
