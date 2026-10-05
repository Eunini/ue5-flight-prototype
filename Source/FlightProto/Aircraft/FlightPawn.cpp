#include "Aircraft/FlightPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "Interaction/CabinInteractionComponent.h"
#include "Interaction/CabinSwitchComponent.h"
#include "Net/UnrealNetwork.h"
#include "Systems/AircraftSystemsComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Demo/PortfolioVisual.h"
#include "Demo/PortfolioCapture.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	constexpr double CmPerMetre = 100.0;

	FVector ToUnrealCm(const FlightCore::Vec3& V) { return FVector(V.X, V.Y, V.Z) * CmPerMetre; }
	FlightCore::Vec3 FromUnrealCm(const FVector& V) { return {V.X / CmPerMetre, V.Y / CmPerMetre, V.Z / CmPerMetre}; }

	// Both sides use X forward / Y right / Z up and the same quaternion layout.
	FVector ToFVector(const FlightCore::Vec3& V) { return FVector(V.X, V.Y, V.Z); }
	FlightCore::Vec3 FromFVector(const FVector& V) { return {V.X, V.Y, V.Z}; }
	FQuat ToFQuat(const FlightCore::Quat& Q) { return FQuat(Q.X, Q.Y, Q.Z, Q.W); }
	FlightCore::Quat FromFQuat(const FQuat& Q) { return {Q.W, Q.X, Q.Y, Q.Z}; }

	int8 QuantizeAxis(float V) { return static_cast<int8>(FMath::RoundToInt(FMath::Clamp(V, -1.f, 1.f) * 127.f)); }
}

FFlightInputFrame FFlightInputFrame::Make(uint32 InSequence, float InThrottle, float InPitch, float InRoll, float InYaw, bool bInBrake)
{
	FFlightInputFrame Frame;
	Frame.Sequence = InSequence;
	Frame.Throttle = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(InThrottle, 0.f, 1.f) * 255.f));
	Frame.Pitch = QuantizeAxis(InPitch);
	Frame.Roll = QuantizeAxis(InRoll);
	Frame.Yaw = QuantizeAxis(InYaw);
	Frame.bBrake = bInBrake;
	return Frame;
}

FlightCore::ControlInput FFlightInputFrame::ToControlInput() const
{
	FlightCore::ControlInput In;
	In.Throttle = Throttle / 255.0;
	In.Pitch = Pitch / 127.0;
	In.Roll = Roll / 127.0;
	In.Yaw = Yaw / 127.0;
	In.bBrake = bBrake;
	return In;
}

AFlightPawn::AFlightPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false); // transform is driven by ServerState, not the default movement replication
	NetUpdateFrequency = 30.f;
	MinNetUpdateFrequency = 10.f;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));

	AircraftRoot = CreateDefaultSubobject<USceneComponent>(TEXT("AircraftRoot"));
	SetRootComponent(AircraftRoot);

	// Blockout geometry from engine cubes (1 m); swap for modular aircraft meshes later.
	auto MakePart = [this](const TCHAR* Name, const FVector& LocationCm, const FVector& SizeMetres)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(AircraftRoot);
		Part->SetStaticMesh(CubeMesh.Object);
		Part->SetRelativeLocation(LocationCm);
		Part->SetRelativeScale3D(SizeMetres);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return Part;
	};
	Fuselage = MakePart(TEXT("Fuselage"), FVector(0, 0, 0), FVector(8.0, 1.2, 1.4));
	Wing = MakePart(TEXT("Wing"), FVector(40, 0, 60), FVector(1.5, 11.0, 0.12));
	Tailplane = MakePart(TEXT("Tailplane"), FVector(-370, 0, 20), FVector(1.0, 3.4, 0.08));
	Fin = MakePart(TEXT("Fin"), FVector(-370, 0, 90), FVector(1.0, 0.1, 1.4));
	InstrumentPanel = MakePart(TEXT("InstrumentPanel"), FVector(95, 0, 15), FVector(0.05, 1.0, 0.3));

 // Original aircraft details share the native presentation components.
 auto Detail=[this](const TCHAR* Name,const TCHAR* Shape,FVector Location,FVector Size,FRotator Rotation=FRotator::ZeroRotator)
 {
  auto* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(Name);
  Mesh->SetupAttachment(AircraftRoot);
  Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,*FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"),Shape,Shape)));
  Mesh->SetRelativeLocation(Location);Mesh->SetRelativeScale3D(Size);Mesh->SetRelativeRotation(Rotation);
  Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);TrimParts.Add(Mesh);return Mesh;
 };
 Detail(TEXT("Nose"),TEXT("Cone"),FVector(410,0,0),FVector(1.2,1.2,1.3),FRotator(90,0,0));
 Detail(TEXT("Canopy"),TEXT("Sphere"),FVector(15,0,85),FVector(2.4,1.1,.95));
 Detail(TEXT("MainWheelL"),TEXT("Cylinder"),FVector(-210,-160,-90),FVector(.6,.6,.22),FRotator(0,0,90));
 Detail(TEXT("MainWheelR"),TEXT("Cylinder"),FVector(-210,160,-90),FVector(.6,.6,.22),FRotator(0,0,90));
 Detail(TEXT("NoseWheel"),TEXT("Cylinder"),FVector(240,0,-90),FVector(.6,.6,.22),FRotator(0,0,90));
 PropellerRoot=CreateDefaultSubobject<USceneComponent>(TEXT("PropellerRoot"));
 PropellerRoot->SetupAttachment(AircraftRoot);PropellerRoot->SetRelativeLocation(FVector(485,0,0));
 auto* BladeA=Detail(TEXT("PropellerBladeA"),TEXT("Cube"),FVector::ZeroVector,FVector(.05,.14,2.0));
 auto* BladeB=Detail(TEXT("PropellerBladeB"),TEXT("Cube"),FVector::ZeroVector,FVector(.05,2.0,.14));
 BladeA->SetupAttachment(PropellerRoot);BladeB->SetupAttachment(PropellerRoot);

	// One switch per cabin control, laid out left to right on the panel.
	const int32 SwitchCount = static_cast<int32>(ECabinSwitch::Count);
	for (int32 i = 0; i < SwitchCount; ++i)
	{
		const FName Name = *FString::Printf(TEXT("PanelSwitch_%d"), i);
		UCabinSwitchComponent* SwitchComp = CreateDefaultSubobject<UCabinSwitchComponent>(Name);
		SwitchComp->SetupAttachment(AircraftRoot);
		SwitchComp->SetStaticMesh(CubeMesh.Object);
		SwitchComp->Switch = static_cast<ECabinSwitch>(i);
		const float Y = FMath::Lerp(-36.f, 36.f, SwitchCount > 1 ? float(i) / (SwitchCount - 1) : 0.5f);
		SwitchComp->SetRelativeLocation(FVector(90, Y, 15));
		SwitchComp->SetRelativeScale3D(FVector(0.03, 0.04, 0.09));
		PanelSwitches.Add(SwitchComp);
	}

	ChaseArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("ChaseArm"));
	ChaseArm->SetupAttachment(AircraftRoot);
	ChaseArm->TargetArmLength = 1500.f;
	ChaseArm->SocketOffset = FVector(0, 0, 250);
	ChaseArm->bDoCollisionTest = false;
	ChaseArm->bEnableCameraLag = true;
	ChaseArm->bEnableCameraRotationLag = true;
	ChaseArm->CameraLagSpeed = 12.f;
	ChaseArm->CameraRotationLagSpeed = 6.f;

	ChaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
	ChaseCamera->SetupAttachment(ChaseArm, USpringArmComponent::SocketName);

	CabinCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CabinCamera"));
	CabinCamera->SetupAttachment(AircraftRoot);
	CabinCamera->SetRelativeLocation(FVector(10, -25, 45));
	CabinCamera->SetFieldOfView(85.f);
	CabinCamera->SetAutoActivate(false);

	Systems = CreateDefaultSubobject<UAircraftSystemsComponent>(TEXT("Systems"));
	Interaction = CreateDefaultSubobject<UCabinInteractionComponent>(TEXT("Interaction"));
}

void AFlightPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFlightPawn, ServerState);
}

void AFlightPawn::BeginPlay()
{
	Super::BeginPlay();
 ApplyAircraftLivery();
 bPortfolioDemo=FParse::Param(FCommandLine::Get(),TEXT("PortfolioDemo"));
 DemoAge=0.f;
 if (bPortfolioDemo && IsLocallyControlled()) { ToggleCabinView(); bShowDiagnostics=true; }
	if (HasAuthority())
	{
		PlaceOnGround();
		PublishServerState();
	}
	else if (!bHasAuthoritativeState)
	{
		PlaceOnGround();
	}
	ApplyStateToActor(0.f);
}

void AFlightPawn::PlaceOnGround()
{
	State = FlightCore::FlightState();
	State.Position = FromUnrealCm(GetActorLocation());
	State.Orientation = FlightCore::Quat::FromYawPitchRoll(FMath::DegreesToRadians(GetActorRotation().Yaw), 0.0, 0.0);
	const double Ground = TraceGroundHeightMetres();
	State.Position.Z = Ground + Model.GetParams().GearHeight;
	State.bOnGround = true;
}

FVector AFlightPawn::GetPawnViewLocation() const
{
	return CabinCamera ? CabinCamera->GetComponentLocation() : Super::GetPawnViewLocation();
}

void AFlightPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	PlayerInputComponent->BindAxis(TEXT("Pitch"), this, &AFlightPawn::SetPitchAxis);
	PlayerInputComponent->BindAxis(TEXT("Roll"), this, &AFlightPawn::SetRollAxis);
	PlayerInputComponent->BindAxis(TEXT("Yaw"), this, &AFlightPawn::SetYawAxis);
	PlayerInputComponent->BindAxis(TEXT("ThrottleChange"), this, &AFlightPawn::SetThrottleRate);
	PlayerInputComponent->BindAxis(TEXT("LookYaw"), this, &AFlightPawn::LookYaw);
	PlayerInputComponent->BindAxis(TEXT("LookPitch"), this, &AFlightPawn::LookPitch);
	PlayerInputComponent->BindAction(TEXT("Brake"), IE_Pressed, this, &AFlightPawn::BrakePressed);
	PlayerInputComponent->BindAction(TEXT("Brake"), IE_Released, this, &AFlightPawn::BrakeReleased);
	PlayerInputComponent->BindAction(TEXT("Interact"), IE_Pressed, this, &AFlightPawn::Interact);
	PlayerInputComponent->BindAction(TEXT("ToggleCabinView"), IE_Pressed, this, &AFlightPawn::ToggleCabinView);
	PlayerInputComponent->BindAction(TEXT("ToggleDiagnostics"), IE_Pressed, this, &AFlightPawn::ToggleDiagnostics);
}

void AFlightPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
 if (bPortfolioDemo && IsLocallyControlled())
 {
  const auto* Capture=GetWorld()->GetSubsystem<UFlightCaptureSubsystem>();
  if (!FParse::Param(FCommandLine::Get(),TEXT("PortfolioCapture")) || (Capture && Capture->IsReady())) AdvanceDemo(DeltaSeconds);
 }
 if (PropellerRoot && Systems && (Systems->GetEngineState()==EEngineState::Running || Systems->GetEngineState()==EEngineState::Cranking))
  PropellerRoot->AddLocalRotation(FRotator(0,0,DeltaSeconds*(Systems->GetEngineState()==EEngineState::Running?1800.f:360.f)));

	if (IsLocallyControlled())
	{
		ThrottleSetting = FMath::Clamp(ThrottleSetting + ThrottleRate * 0.5f * DeltaSeconds, 0.f, 1.f);

		FrameAccumulator += DeltaSeconds;
		int32 FramesThisTick = 0;
		while (FrameAccumulator >= FrameSeconds && FramesThisTick < 5)
		{
			FrameAccumulator -= FrameSeconds;
			FixedUpdate();
			++FramesThisTick;
		}
		FrameAccumulator = FMath::Min(FrameAccumulator, FrameSeconds); // drop time after a hitch rather than spiral
		ApplyStateToActor(DeltaSeconds);
	}
	else if (HasAuthority())
	{
		// Remote pilot: the server advances only when inputs arrive; this refills how much it may advance.
		ServerTimeBudget = FMath::Min(ServerTimeBudget + DeltaSeconds, MaxServerCatchUpSeconds);
		ApplyStateToActor(DeltaSeconds);
	}
	else
	{
		UpdateSimulatedProxy();
	}
}

void AFlightPawn::FixedUpdate()
{
	const FFlightInputFrame Frame = FFlightInputFrame::Make(NextInputSequence++, ThrottleSetting, PitchAxis, RollAxis, YawAxis, bBrakeHeld);
	SimulateFrame(Frame);

	if (HasAuthority())
	{
		// Listen-server host flies with no latency.
		ServerState.LastProcessedInput = Frame.Sequence;
		PublishServerState();
		return;
	}

	PendingInputs.Add(Frame);
	// A client that stops hearing from the server should not buffer forever.
	if (PendingInputs.Num() > 240)
	{
		PendingInputs.RemoveAt(0, PendingInputs.Num() - 240);
	}
	const int32 Count = FMath::Min(RedundantInputsPerRpc, PendingInputs.Num());
	TArray<FFlightInputFrame> ToSend(PendingInputs.GetData() + PendingInputs.Num() - Count, Count);
	Server_SendInputs(ToSend);
}

void AFlightPawn::SimulateFrame(const FFlightInputFrame& Frame)
{
	const FlightCore::ControlInput Input = Frame.ToControlInput();
	if (HasAuthority() && Systems)
	{
		Systems->SetThrottle(static_cast<float>(Input.Throttle));
	}

	FlightCore::StepContext Ctx;
	Ctx.Dt = FrameSeconds / SubStepsPerFrame;
	Ctx.GroundHeight = TraceGroundHeightMetres();
	Ctx.EngineThrustScale = Systems ? Systems->GetThrustScale() : 0.0;
	for (int32 i = 0; i < SubStepsPerFrame; ++i)
	{
		LastTelemetry = Model.Step(State, Input, Ctx);
	}
}

double AFlightPawn::TraceGroundHeightMetres() const
{
	const FVector Start = ToUnrealCm(State.Position) + FVector(0, 0, 200);
	const FVector End = Start - FVector(0, 0, 2000000);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FlightGroundTrace), false, this);
	if (GetWorld() && GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		return Hit.ImpactPoint.Z / CmPerMetre;
	}
	return -100000.0; // nothing below: effectively no ground
}

void AFlightPawn::Server_SendInputs_Implementation(const TArray<FFlightInputFrame>& Inputs)
{
	bool bAdvanced = false;
	for (const FFlightInputFrame& Frame : Inputs)
	{
		if (Frame.Sequence <= ServerState.LastProcessedInput)
		{
			continue; // duplicate from redundancy or reordered packet
		}
		if (ServerTimeBudget < FrameSeconds)
		{
			break; // client is asking for more simulated time than has passed: speed-hack guard
		}
		SimulateFrame(Frame);
		ServerTimeBudget -= FrameSeconds;
		ServerState.LastProcessedInput = Frame.Sequence;
		bAdvanced = true;
	}
	if (bAdvanced)
	{
		PublishServerState();
	}
}

void AFlightPawn::PublishServerState()
{
	ServerState.Position = ToFVector(State.Position);
	ServerState.Velocity = ToFVector(State.Velocity);
	ServerState.Orientation = ToFQuat(State.Orientation);
	ServerState.AngularVelocity = ToFVector(State.AngularVelocity);
	ServerState.bOnGround = State.bOnGround;
}

void AFlightPawn::OnRep_ServerState()
{
	FlightCore::FlightState Authoritative;
	Authoritative.Position = FromFVector(ServerState.Position);
	Authoritative.Velocity = FromFVector(ServerState.Velocity);
	Authoritative.Orientation = FromFQuat(ServerState.Orientation);
	Authoritative.AngularVelocity = FromFVector(ServerState.AngularVelocity);
	Authoritative.bOnGround = ServerState.bOnGround;

	if (!IsLocallyControlled())
	{
		// Other players' aircraft: keep a short history to interpolate through.
		State = Authoritative;
		bHasAuthoritativeState = true;
		ProxySnapshots.Add({GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0, ToUnrealCm(State.Position), ToFQuat(State.Orientation)});
		if (ProxySnapshots.Num() > 32)
		{
			ProxySnapshots.RemoveAt(0);
		}
		return;
	}

	// Owning client: rewind to the server's state and replay what it has not seen yet.
	const FVector RenderedLocation = GetActorLocation();
	const FQuat RenderedRotation = GetActorQuat();

	State = Authoritative;
	bHasAuthoritativeState = true;
	PendingInputs.RemoveAll([this](const FFlightInputFrame& F) { return F.Sequence <= ServerState.LastProcessedInput; });
	for (const FFlightInputFrame& Frame : PendingInputs)
	{
		SimulateFrame(Frame);
	}

	const FVector CorrectedLocation = ToUnrealCm(State.Position);
	const FQuat CorrectedRotation = ToFQuat(State.Orientation);
	const FVector Error = RenderedLocation - CorrectedLocation;
	LastCorrectionCm = Error.Size();
	if (LastCorrectionCm > SnapThresholdCm)
	{
		VisualLocationOffset = FVector::ZeroVector;
		VisualRotationOffset = FQuat::Identity;
	}
	else
	{
		// Keep drawing where we were and bleed the difference off over a few frames.
		VisualLocationOffset = Error;
		VisualRotationOffset = RenderedRotation * CorrectedRotation.Inverse();
	}
}

void AFlightPawn::ApplyStateToActor(float DeltaSeconds)
{
	const float Decay = FMath::Exp(-12.f * DeltaSeconds);
	VisualLocationOffset *= Decay;
	VisualRotationOffset = FQuat::Slerp(FQuat::Identity, VisualRotationOffset, Decay);

	SetActorLocationAndRotation(ToUnrealCm(State.Position) + VisualLocationOffset,
		VisualRotationOffset * ToFQuat(State.Orientation));
}

void AFlightPawn::UpdateSimulatedProxy()
{
	if (ProxySnapshots.Num() == 0)
	{
		return;
	}
	const double RenderTime = GetWorld()->GetTimeSeconds() - ProxyInterpolationDelay;

	// Drop snapshots that are entirely in the past, keeping one before RenderTime.
	while (ProxySnapshots.Num() > 2 && ProxySnapshots[1].ReceivedAt <= RenderTime)
	{
		ProxySnapshots.RemoveAt(0);
	}

	const FProxySnapshot& A = ProxySnapshots[0];
	if (ProxySnapshots.Num() == 1 || RenderTime <= A.ReceivedAt)
	{
		SetActorLocationAndRotation(A.Location, A.Rotation);
		return;
	}
	const FProxySnapshot& B = ProxySnapshots[1];
	const double Span = FMath::Max(B.ReceivedAt - A.ReceivedAt, 1e-4);
	const float Alpha = static_cast<float>(FMath::Clamp((RenderTime - A.ReceivedAt) / Span, 0.0, 1.0));
	SetActorLocationAndRotation(FMath::Lerp(A.Location, B.Location, Alpha), FQuat::Slerp(A.Rotation, B.Rotation, Alpha));
}

void AFlightPawn::LookYaw(float Value)
{
	if (bCabinView && Value != 0.f)
	{
		CabinLook.Yaw = FMath::Clamp(CabinLook.Yaw + Value, -120.f, 120.f);
		CabinCamera->SetRelativeRotation(CabinLook);
	}
}

void AFlightPawn::LookPitch(float Value)
{
	if (bCabinView && Value != 0.f)
	{
		CabinLook.Pitch = FMath::Clamp(CabinLook.Pitch + Value, -60.f, 45.f);
		CabinCamera->SetRelativeRotation(CabinLook);
	}
}

void AFlightPawn::Interact()
{
	if (Interaction)
	{
		Interaction->TryInteract();
	}
}

void AFlightPawn::ToggleCabinView()
{
	bCabinView = !bCabinView;
	CabinCamera->SetActive(bCabinView);
	ChaseCamera->SetActive(!bCabinView);
}

void AFlightPawn::ApplyAircraftLivery()
{
 PortfolioVisual::Color(Fuselage,FLinearColor(.72f,.8f,.81f));
 PortfolioVisual::Color(Wing,FLinearColor(.94f,.3f,.07f));
 PortfolioVisual::Color(Tailplane,FLinearColor(.94f,.3f,.07f));
 PortfolioVisual::Color(Fin,FLinearColor(.05f,.09f,.14f));
 PortfolioVisual::Color(InstrumentPanel,FLinearColor(.025f,.035f,.04f));
 for (UStaticMeshComponent* Part:TrimParts)
  PortfolioVisual::Color(Part,FLinearColor(.055f,.12f,.16f));
 for (UCabinSwitchComponent* Switch:PanelSwitches) PortfolioVisual::Color(Switch,FLinearColor(.9f,.67f,.23f));
}

void AFlightPawn::AdvanceDemo(float Delta)
{
 DemoAge+=Delta;const float Age=DemoAge;
 const ECabinSwitch Steps[]={ECabinSwitch::Battery,ECabinSwitch::FuelPump,ECabinSwitch::Magnetos,ECabinSwitch::Starter};
 if (HasAuthority() && DemoSwitchStep<4 && Age>1.f+DemoSwitchStep)
  Systems->OperateSwitch(Steps[DemoSwitchStep++]);
 if (Age>8.f && bCabinView) { ToggleCabinView(); bShowDiagnostics=false; }
 ThrottleSetting=Age>8.f?1.f:0.f;
 bBrakeHeld=Age<8.f;
 const auto BodyVelocity=State.Orientation.Unrotate(State.Velocity);
 const double Speed=BodyVelocity.Length();
 if (State.bOnGround) { PitchAxis=Speed>24.?0.6f:0.f; RollAxis=0; YawAxis=0; }
 else
 {
  const double Roll=State.Orientation.RollRad()/FlightCore::DegToRad;
  const double Pitch=State.Orientation.PitchRad()/FlightCore::DegToRad;
  const double TargetPitch=FlightCore::Clamp(8.+(Speed-(State.Position.Z<250.?36.:44.))*1.5,2.,12.);
  const double TargetRoll=State.Position.Z>25. && Age>30.f?25.:0.;
  PitchAxis=static_cast<float>(FlightCore::Clamp((TargetPitch-Pitch)*.1+FMath::Abs(Roll)*.01,-1.,1.));
  RollAxis=static_cast<float>(FlightCore::Clamp((TargetRoll-Roll)*.03,-1.,1.));
  YawAxis=static_cast<float>(FlightCore::Clamp(BodyVelocity.Y*.03,-.5,.5));
 }
}
