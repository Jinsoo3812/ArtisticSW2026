#include "Upgrade/ShipUpgradeBlueprintLibrary.h"

#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Upgrade/ShipUpgradeComponent.h"

UShipUpgradeComponent* UShipUpgradeBlueprintLibrary::GetLocalShipUpgradeComponent(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject) return nullptr;
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	return PlayerController ? GetShipUpgradeComponent(PlayerController->PlayerState) : nullptr;
}

UShipUpgradeComponent* UShipUpgradeBlueprintLibrary::GetShipUpgradeComponent(APlayerState* PlayerState)
{
	return PlayerState ? PlayerState->FindComponentByClass<UShipUpgradeComponent>() : nullptr;
}

FCraftingItemStack UShipUpgradeBlueprintLibrary::MakeShipUpgradeMaterialCost(FName ItemTagName, int32 Quantity)
{
	FCraftingItemStack Cost;
	Cost.ItemTag = FGameplayTag::RequestGameplayTag(ItemTagName, false);
	Cost.Quantity = Quantity;
	return Cost;
}
