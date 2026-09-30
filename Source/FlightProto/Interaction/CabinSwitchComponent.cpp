#include "Interaction/CabinSwitchComponent.h"

#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "CabinSwitch"

UCabinSwitchComponent::UCabinSwitchComponent()
{
	SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	SetGenerateOverlapEvents(false);
}

void UCabinSwitchComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UAircraftSystemsComponent* Systems = GetSystems())
	{
		Systems->OnSystemsChanged.AddDynamic(this, &UCabinSwitchComponent::RefreshVisual);
	}
	RefreshVisual();
}

UAircraftSystemsComponent* UCabinSwitchComponent::GetSystems() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UAircraftSystemsComponent>() : nullptr;
}

FText UCabinSwitchComponent::GetInteractionPrompt(const APawn* User) const
{
	const UAircraftSystemsComponent* Systems = GetSystems();
	if (!Systems)
	{
		return FText::GetEmpty();
	}
	if (Switch == ECabinSwitch::Starter)
	{
		return FText::Format(LOCTEXT("PressPrompt", "[F] {0}: press"), Systems->GetSwitchLabel(Switch));
	}
	return FText::Format(LOCTEXT("TogglePrompt", "[F] {0}: turn {1}"),
		Systems->GetSwitchLabel(Switch),
		Systems->IsSwitchOn(Switch) ? LOCTEXT("Off", "OFF") : LOCTEXT("On", "ON"));
}

bool UCabinSwitchComponent::CanInteract(const APawn* User) const
{
	// Only occupants of this aircraft can operate its panel.
	return User != nullptr && User == GetOwner() && GetSystems() != nullptr;
}

void UCabinSwitchComponent::Interact(APawn* User)
{
	if (UAircraftSystemsComponent* Systems = GetSystems())
	{
		Systems->OperateSwitch(Switch);
	}
}

void UCabinSwitchComponent::RefreshVisual()
{
	const UAircraftSystemsComponent* Systems = GetSystems();
	const bool bOn = Systems && Systems->IsSwitchOn(Switch);
	SetRelativeRotation(FRotator(bOn ? -ThrowAngleDegrees : ThrowAngleDegrees, 0.f, 0.f));
}

#undef LOCTEXT_NAMESPACE
