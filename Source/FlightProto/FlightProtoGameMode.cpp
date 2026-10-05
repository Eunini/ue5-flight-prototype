#include "FlightProtoGameMode.h"

#include "Aircraft/FlightPawn.h"
#include "UI/FlightHUD.h"
#include "Demo/FlightDemoWorld.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

AFlightProtoGameMode::AFlightProtoGameMode()
{
	DefaultPawnClass = AFlightPawn::StaticClass();
 if (!IsRunningCommandlet())
 {
  static ConstructorHelpers::FClassFinder<APawn> Blueprint(TEXT("/Game/FlightDemo/Blueprints/BP_Aircraft"));
   if (Blueprint.Succeeded()) DefaultPawnClass=Blueprint.Class;
 }
	HUDClass = AFlightHUD::StaticClass();
}

void AFlightProtoGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
 Super::InitGame(MapName,Options,ErrorMessage);
 if (!TActorIterator<AFlightDemoWorld>(GetWorld())) GetWorld()->SpawnActor<AFlightDemoWorld>();
}

APawn* AFlightProtoGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	FTransform Parked = SpawnTransform;
	Parked.AddToTranslation(SpawnTransform.GetRotation().GetRightVector() * ParkingSpacingCm * SpawnedCount++);
	return Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayer, Parked);
}
