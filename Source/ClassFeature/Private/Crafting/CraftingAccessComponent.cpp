#include "Crafting/CraftingAccessComponent.h"

UCraftingAccessComponent::UCraftingAccessComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UCraftingAccessComponent::IsExternalReceiverAllowed(const AActor* Receiver) const
{
	return Receiver && AllowedExternalReceivers.Contains(Receiver);
}

