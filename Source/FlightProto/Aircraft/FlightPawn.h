// Player aircraft. Flight is simulated with the deterministic FlightCore model using a
// server-authoritative scheme:
//   * owning client  - predicts locally, sends quantised inputs, replays unacknowledged
//                      inputs when the server state arrives, smooths the visual error.
//   * server         - simulates only inputs it has received, bounded by a time budget.
//   * other clients  - interpolate between server snapshots with a small delay.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Core/FlightModel.h"
#include "FlightPawn.generated.h"

class UAircraftSystemsComponent;
class UCabinInteractionComponent;
class UCabinSwitchComponent;
class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;

/** One fixed simulation frame of pilot input, quantised so client and server simulate identical values. */
USTRUCT()
struct FFlightInputFrame
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 Sequence = 0;

	UPROPERTY()
	uint8 Throttle = 0;

	UPROPERTY()
	int8 Pitch = 0;

	UPROPERTY()
	int8 Roll = 0;

	UPROPERTY()
	int8 Yaw = 0;

	UPROPERTY()
	bool bBrake = false;

	static FFlightInputFrame Make(uint32 InSequence, float InThrottle, float InPitch, float InRoll, float InYaw, bool bInBrake);
	FlightCore::ControlInput ToControlInput() const;
};

/** Authoritative state sent to clients. Values are the flight model's own (metres, doubles) so replay is exact. */
USTRUCT()
struct FNetFlightState
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 LastProcessedInput = 0;

	UPROPERTY()
	FVector Position = FVector::ZeroVector;

	UPROPERTY()
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY()
	FQuat Orientation = FQuat::Identity;

	UPROPERTY()
	FVector AngularVelocity = FVector::ZeroVector;

	UPROPERTY()
	bool bOnGround = true;
};

UCLASS()
class FLIGHTPROTO_API AFlightPawn : public APawn
{
	GENERATED_BODY()

public:
	AFlightPawn();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual FVector GetPawnViewLocation() const override;

	const FlightCore::Telemetry& GetTelemetry() const { return LastTelemetry; }
	float GetAltitudeMetres() const { return static_cast<float>(State.Position.Z); }
	float GetThrottleSetting() const { return ThrottleSetting; }
	bool IsInCabinView() const { return bCabinView; }
	bool IsDiagnosticsOpen() const { return bShowDiagnostics; }
	int32 GetUnacknowledgedInputCount() const { return PendingInputs.Num(); }
	float GetLastCorrectionCm() const { return LastCorrectionCm; }
	UAircraftSystemsComponent* GetSystems() const { return Systems; }
	UCabinInteractionComponent* GetInteraction() const { return Interaction; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Aircraft")
	TObjectPtr<USceneComponent> AircraftRoot;

	UPROPERTY(VisibleAnywhere, Category = "Aircraft")
	TObjectPtr<UStaticMeshComponent> Fuselage;

	UPROPERTY(VisibleAnywhere, Category = "Aircraft")
	TObjectPtr<UStaticMeshComponent> Wing;

	UPROPERTY(VisibleAnywhere, Category = "Aircraft")
	TObjectPtr<UStaticMeshComponent> Tailplane;

	UPROPERTY(VisibleAnywhere, Category = "Aircraft")
	TObjectPtr<UStaticMeshComponent> Fin;

	UPROPERTY(VisibleAnywhere, Category = "Aircraft|Cabin")
	TObjectPtr<UStaticMeshComponent> InstrumentPanel;

	UPROPERTY(VisibleAnywhere, Category = "Aircraft|Cabin")
	TArray<TObjectPtr<UCabinSwitchComponent>> PanelSwitches;

	UPROPERTY(VisibleAnywhere, Category = "Aircraft|Camera")
	TObjectPtr<USpringArmComponent> ChaseArm;

	UPROPERTY(VisibleAnywhere, Category = "Aircraft|Camera")
	TObjectPtr<UCameraComponent> ChaseCamera;

	UPROPERTY(VisibleAnywhere, Category = "Aircraft|Camera")
	TObjectPtr<UCameraComponent> CabinCamera;

	UPROPERTY(VisibleAnywhere, Category = "Aircraft")
	TObjectPtr<UAircraftSystemsComponent> Systems;

	UPROPERTY(VisibleAnywhere, Category = "Aircraft")
	TObjectPtr<UCabinInteractionComponent> Interaction;

	/** Render delay used when interpolating other players' aircraft. */
	UPROPERTY(EditDefaultsOnly, Category = "Flight|Network")
	float ProxyInterpolationDelay = 0.1f;

	/** Recent inputs re-sent in every RPC so a lost packet does not stall the server. */
	UPROPERTY(EditDefaultsOnly, Category = "Flight|Network")
	int32 RedundantInputsPerRpc = 6;

	/** Largest burst of simulation time a client may ask the server to catch up on. */
	UPROPERTY(EditDefaultsOnly, Category = "Flight|Network")
	float MaxServerCatchUpSeconds = 0.25f;

	/** Corrections larger than this snap instead of blending. */
	UPROPERTY(EditDefaultsOnly, Category = "Flight|Network")
	float SnapThresholdCm = 1000.f;

private:
	static constexpr double FrameSeconds = 1.0 / 60.0;
	static constexpr int32 SubStepsPerFrame = 2;

	void FixedUpdate();
	void SimulateFrame(const FFlightInputFrame& Frame);
	double TraceGroundHeightMetres() const;
	void PlaceOnGround();
	void PublishServerState();
	void ApplyStateToActor(float DeltaSeconds);
	void UpdateSimulatedProxy();

	UFUNCTION(Server, Unreliable)
	void Server_SendInputs(const TArray<FFlightInputFrame>& Inputs);

	UFUNCTION()
	void OnRep_ServerState();

	UPROPERTY(ReplicatedUsing = OnRep_ServerState)
	FNetFlightState ServerState;

	// Input handlers.
	void SetPitchAxis(float Value) { PitchAxis = Value; }
	void SetRollAxis(float Value) { RollAxis = Value; }
	void SetYawAxis(float Value) { YawAxis = Value; }
	void SetThrottleRate(float Value) { ThrottleRate = Value; }
	void LookYaw(float Value);
	void LookPitch(float Value);
	void BrakePressed() { bBrakeHeld = true; }
	void BrakeReleased() { bBrakeHeld = false; }
	void Interact();
	void ToggleCabinView();
	void ToggleDiagnostics() { bShowDiagnostics = !bShowDiagnostics; }

	FlightCore::FlightModel Model;
	FlightCore::FlightState State;
	FlightCore::Telemetry LastTelemetry;

	TArray<FFlightInputFrame> PendingInputs;
	uint32 NextInputSequence = 1;
	double FrameAccumulator = 0.0;
	float ServerTimeBudget = 0.f;
	bool bHasAuthoritativeState = false;

	// Visual smoothing of prediction corrections (owning client).
	FVector VisualLocationOffset = FVector::ZeroVector;
	FQuat VisualRotationOffset = FQuat::Identity;
	float LastCorrectionCm = 0.f;

	struct FProxySnapshot
	{
		double ReceivedAt = 0.0;
		FVector Location;
		FQuat Rotation;
	};
	TArray<FProxySnapshot> ProxySnapshots;

	float ThrottleSetting = 0.f;
	float ThrottleRate = 0.f;
	float PitchAxis = 0.f;
	float RollAxis = 0.f;
	float YawAxis = 0.f;
	bool bBrakeHeld = false;
	bool bCabinView = false;
	bool bShowDiagnostics = false;
	FRotator CabinLook = FRotator::ZeroRotator;
};
