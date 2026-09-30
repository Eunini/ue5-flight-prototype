#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CabinInteractionComponent.generated.h"

/**
 * Finds the interactable the local player is looking at and forwards use requests to
 * the server, which re-validates reach and permission before acting.
 */
UCLASS(ClassGroup = (Aircraft), meta = (BlueprintSpawnableComponent))
class FLIGHTPROTO_API UCabinInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCabinInteractionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Local player pressed the interact key. */
	void TryInteract();

	/** Prompt for the HUD; empty when nothing usable is in view. */
	FText GetCurrentPrompt() const;

	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	float ReachCm = 250.f;

private:
	bool GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const;
	APawn* GetPawn() const;

	UFUNCTION(Server, Reliable)
	void Server_Interact(UPrimitiveComponent* Target);

	TWeakObjectPtr<UPrimitiveComponent> Focused;
};
