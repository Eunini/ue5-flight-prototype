#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Interaction/Interactable.h"
#include "Systems/AircraftSystemsComponent.h"
#include "CabinSwitchComponent.generated.h"

/**
 * A physical switch on the cabin panel. It holds no state of its own: the aircraft
 * systems component is the single source of truth and this mesh just mirrors it.
 */
UCLASS(ClassGroup = (Aircraft), meta = (BlueprintSpawnableComponent))
class FLIGHTPROTO_API UCabinSwitchComponent : public UStaticMeshComponent, public IInteractable
{
	GENERATED_BODY()

public:
	UCabinSwitchComponent();

	virtual FText GetInteractionPrompt(const APawn* User) const override;
	virtual bool CanInteract(const APawn* User) const override;
	virtual void Interact(APawn* User) override;

	UPROPERTY(EditAnywhere, Category = "Cabin")
	ECabinSwitch Switch = ECabinSwitch::Battery;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void RefreshVisual();

	UAircraftSystemsComponent* GetSystems() const;

	UPROPERTY(EditAnywhere, Category = "Cabin")
	float ThrowAngleDegrees = 35.f;
};
