// Server-authoritative aircraft systems: electrical, fuel and engine start sequence.
// Also produces the diagnostic entries shown on the cabin diagnostic panel.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AircraftSystemsComponent.generated.h"

UENUM(BlueprintType)
enum class ECabinSwitch : uint8
{
	Battery,
	FuelPump,
	Magnetos,
	Starter,     // momentary: triggers a start attempt
	NavLights,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EEngineState : uint8
{
	Off,
	Cranking,
	Running,
	Failed
};

UENUM(BlueprintType)
enum class EEngineFault : uint8
{
	None,
	NoElectricalPower,
	NoFuelPressure,
	NoIgnition,
	FuelExhausted,
	BatteryFlat
};

UENUM(BlueprintType)
enum class EDiagnosticSeverity : uint8
{
	Normal,
	Advisory,
	Warning,
	Fault
};

USTRUCT(BlueprintType)
struct FDiagnosticEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FText Label;

	UPROPERTY(BlueprintReadOnly)
	FText Value;

	UPROPERTY(BlueprintReadOnly)
	EDiagnosticSeverity Severity = EDiagnosticSeverity::Normal;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAircraftSystemsChanged);

UCLASS(ClassGroup = (Aircraft), meta = (BlueprintSpawnableComponent))
class FLIGHTPROTO_API UAircraftSystemsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAircraftSystemsComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Server only. Toggles a latching switch or presses a momentary one. */
	void OperateSwitch(ECabinSwitch Switch);

	/** Server only. Throttle drives RPM and fuel flow while the engine runs. */
	void SetThrottle(float InThrottle) { Throttle = FMath::Clamp(InThrottle, 0.f, 1.f); }

	UFUNCTION(BlueprintPure, Category = "Aircraft|Systems")
	bool IsSwitchOn(ECabinSwitch Switch) const { return (SwitchBits & SwitchMask(Switch)) != 0; }

	UFUNCTION(BlueprintPure, Category = "Aircraft|Systems")
	EEngineState GetEngineState() const { return EngineState; }

	/** 0..1 multiplier fed into the flight model. Replicated, so clients predict the same thrust. */
	UFUNCTION(BlueprintPure, Category = "Aircraft|Systems")
	float GetThrustScale() const { return EngineState == EEngineState::Running ? 1.f : 0.f; }

	UFUNCTION(BlueprintPure, Category = "Aircraft|Systems")
	FText GetSwitchLabel(ECabinSwitch Switch) const;

	/** Rows for the cabin diagnostic menu. */
	UFUNCTION(BlueprintCallable, Category = "Aircraft|Systems")
	TArray<FDiagnosticEntry> BuildDiagnostics() const;

	UPROPERTY(BlueprintAssignable, Category = "Aircraft|Systems")
	FOnAircraftSystemsChanged OnSystemsChanged;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "Aircraft|Systems")
	float FuelCapacityLitres = 150.f;

	UPROPERTY(EditDefaultsOnly, Category = "Aircraft|Systems")
	float FuelBurnLitresPerHourAtFullThrottle = 38.f;

	UPROPERTY(EditDefaultsOnly, Category = "Aircraft|Systems")
	float CrankDurationSeconds = 2.f;

	/** Starter drain per second while cranking, as a fraction of full charge. */
	UPROPERTY(EditDefaultsOnly, Category = "Aircraft|Systems")
	float StarterDrainPerSecond = 0.04f;

private:
	static uint8 SwitchMask(ECabinSwitch Switch) { return static_cast<uint8>(1u << static_cast<uint8>(Switch)); }

	void SetEngineState(EEngineState NewState, EEngineFault Fault = EEngineFault::None);
	EEngineFault CheckRunConditions() const;
	void Notify();

	UFUNCTION()
	void OnRep_Systems();

	UPROPERTY(ReplicatedUsing = OnRep_Systems)
	uint8 SwitchBits = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Systems)
	EEngineState EngineState = EEngineState::Off;

	UPROPERTY(ReplicatedUsing = OnRep_Systems)
	EEngineFault LastFault = EEngineFault::None;

	UPROPERTY(Replicated)
	float FuelLitres = 0.f;

	UPROPERTY(Replicated)
	float BatteryCharge = 1.f;

	UPROPERTY(Replicated)
	float EngineRpm = 0.f;

	float Throttle = 0.f;
	float CrankTimer = 0.f;
};
