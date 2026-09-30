#include "Interaction/CabinInteractionComponent.h"

#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Interaction/Interactable.h"

namespace
{
	IInteractable* AsInteractable(UPrimitiveComponent* Component)
	{
		return Component ? Cast<IInteractable>(Component) : nullptr;
	}

	// Extra reach tolerated on the server for latency between the client's trace and the RPC.
	constexpr float ServerReachSlackCm = 150.f;
}

UCabinInteractionComponent::UCabinInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

APawn* UCabinInteractionComponent::GetPawn() const
{
	return Cast<APawn>(GetOwner());
}

bool UCabinInteractionComponent::GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	const APawn* Pawn = GetPawn();
	const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (!Controller)
	{
		return false;
	}
	Controller->GetPlayerViewPoint(OutLocation, OutRotation);
	return true;
}

void UCabinInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	Focused = nullptr;
	const APawn* Pawn = GetPawn();
	if (!Pawn || !Pawn->IsLocallyControlled())
	{
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	if (!GetViewPoint(ViewLocation, ViewRotation))
	{
		return;
	}

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CabinInteraction), false);
	const FVector End = ViewLocation + ViewRotation.Vector() * ReachCm;
	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, End, ECC_Visibility, Params))
	{
		IInteractable* Interactable = AsInteractable(Hit.GetComponent());
		if (Interactable && Interactable->CanInteract(Pawn))
		{
			Focused = Hit.GetComponent();
		}
	}
}

FText UCabinInteractionComponent::GetCurrentPrompt() const
{
	if (IInteractable* Interactable = AsInteractable(Focused.Get()))
	{
		return Interactable->GetInteractionPrompt(GetPawn());
	}
	return FText::GetEmpty();
}

void UCabinInteractionComponent::TryInteract()
{
	if (UPrimitiveComponent* Target = Focused.Get())
	{
		Server_Interact(Target);
	}
}

void UCabinInteractionComponent::Server_Interact_Implementation(UPrimitiveComponent* Target)
{
	APawn* Pawn = GetPawn();
	IInteractable* Interactable = AsInteractable(Target);
	if (!Pawn || !Interactable)
	{
		return;
	}

	// Never trust the client's target: re-check reach and permission on the server.
	// The pawn's eye point is used because the server has no reliable copy of the client camera.
	const float Distance = FVector::Dist(Pawn->GetPawnViewLocation(), Target->GetComponentLocation());
	if (Distance > ReachCm + ServerReachSlackCm || !Interactable->CanInteract(Pawn))
	{
		return;
	}
	Interactable->Interact(Pawn);
}
