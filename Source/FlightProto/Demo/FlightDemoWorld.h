#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlightDemoWorld.generated.h"

UCLASS()
class FLIGHTPROTO_API AFlightDemoWorld : public AActor
{
 GENERATED_BODY()
public:
 AFlightDemoWorld();
 virtual void OnConstruction(const FTransform& Transform) override;
 virtual void BeginPlay() override;
};
