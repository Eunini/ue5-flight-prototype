#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FlightProtoGameMode.generated.h"

UCLASS()
class FLIGHTPROTO_API AFlightProtoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFlightProtoGameMode();

	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;

private:
	/** Parks each joining aircraft beside the previous one instead of on top of it. */
	UPROPERTY(EditDefaultsOnly, Category = "Spawning")
	float ParkingSpacingCm = 2000.f;

	int32 SpawnedCount = 0;
};
