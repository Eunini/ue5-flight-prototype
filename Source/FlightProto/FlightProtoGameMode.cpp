#include "FlightProtoGameMode.h"

#include "Aircraft/FlightPawn.h"
#include "UI/FlightHUD.h"

AFlightProtoGameMode::AFlightProtoGameMode()
{
	DefaultPawnClass = AFlightPawn::StaticClass();
	HUDClass = AFlightHUD::StaticClass();
}

APawn* AFlightProtoGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	FTransform Parked = SpawnTransform;
	Parked.AddToTranslation(SpawnTransform.GetRotation().GetRightVector() * ParkingSpacingCm * SpawnedCount++);
	return Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayer, Parked);
}
