// Anything in the cabin the player can look at and use (switches, levers, doors).
// Implemented by components so one actor can own many interactables.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interactable.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UInteractable : public UInterface
{
	GENERATED_BODY()
};

class FLIGHTPROTO_API IInteractable
{
	GENERATED_BODY()

public:
	/** Text for the on-screen prompt, e.g. "Fuel pump: turn ON". Called on the local client. */
	virtual FText GetInteractionPrompt(const APawn* User) const { return FText::GetEmpty(); }

	/** Checked on the client for the prompt and again on the server before Interact. */
	virtual bool CanInteract(const APawn* User) const { return true; }

	/** Server only. */
	virtual void Interact(APawn* User) {}
};
