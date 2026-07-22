#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Crafting/CraftingTypes.h"
#include "Crafting/CraftingRecipeTypes.h"
#include "CraftingComponent.generated.h"

class UInventoryComponent;

/** New UI-facing crafting service. This class has no dependency on legacy UCrafterComponent. */
UCLASS(ClassGroup = (Crafting), meta = (BlueprintSpawnableComponent))
class CLASSFEATURE_API UCraftingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCraftingComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Crafting|Query")
	TArray<FCraftingListEntry> GetCraftableList(const FCraftingListQuery& Query) const;

	UFUNCTION(BlueprintCallable, Category = "Crafting|Query")
	bool GetCraftingDetails(FName RecipeId, int32 CraftCount, FCraftingDetailsView& OutDetails) const;

	UFUNCTION(BlueprintPure, Category = "Crafting|Query")
	int32 GetOwnedItemCount(FGameplayTag ItemTag) const;

	UFUNCTION(BlueprintPure, Category = "Crafting")
	AActor* GetCurrentCraftingContext() const { return CurrentCraftingContext.Get(); }

	UFUNCTION(BlueprintPure, Category = "Crafting")
	FCraftingResult GetLastCraftingResult() const { return LastCraftingResult; }

	/** Called by the integrated hub UI when its crafting tab is selected. */
	UFUNCTION(BlueprintCallable, Category = "Crafting")
	void OpenCraftingScreen(AActor* ContextActor);

	/** Called when leaving the crafting tab or closing the integrated hub UI. */
	UFUNCTION(BlueprintCallable, Category = "Crafting")
	void CloseCraftingScreen();

	UFUNCTION(BlueprintCallable, Category = "Crafting")
	void RequestCraft(FCraftingRequest Request);

	UPROPERTY(BlueprintAssignable, Category = "Crafting|Events")
	FOnCraftingScreenOpened OnCraftingScreenOpened;

	UPROPERTY(BlueprintAssignable, Category = "Crafting|Events")
	FOnCraftingScreenClosed OnCraftingScreenClosed;

	UPROPERTY(BlueprintAssignable, Category = "Crafting|Events")
	FOnCraftingResult OnCraftingResult;

	UPROPERTY(BlueprintAssignable, Category = "Crafting|Events")
	FOnCraftingDataChanged OnCraftingDataChanged;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Crafting", meta = (ClampMin = "1", ClampMax = "999"))
	int32 MaxCraftCountPerRequest = 99;

	TWeakObjectPtr<AActor> CurrentCraftingContext;
	TWeakObjectPtr<UInventoryComponent> CachedInventory;
	TSet<FGuid> ProcessedRequestIds;

	UPROPERTY(Transient)
	FCraftingResult LastCraftingResult;

	UFUNCTION(Server, Reliable)
	void Server_RequestCraft(const FCraftingRequest& Request);

	UFUNCTION(Server, Reliable)
	void Server_OpenCraftingScreen(AActor* ContextActor);

	UFUNCTION(Server, Reliable)
	void Server_CloseCraftingScreen();

	UFUNCTION(Client, Reliable)
	void Client_OpenCraftingScreen(AActor* ContextActor);

	UFUNCTION(Client, Reliable)
	void Client_CloseCraftingScreen();

	UFUNCTION(Client, Reliable)
	void Client_NotifyCraftingResult(const FCraftingResult& Result);

private:
	UInventoryComponent* ResolveInventory() const;
	FCraftingListEntry BuildListEntry(FName RecipeId, int32 CraftCount) const;
	ECraftingAvailability EvaluateAvailability(const FCraftingRecipeRow& Recipe, int32 CraftCount) const;
	bool BuildCosts(const FCraftingRecipeRow& Recipe, int32 CraftCount, TArray<FCraftingItemStack>& OutCosts) const;
	void ProcessCraftRequest(const FCraftingRequest& Request);
	void CompleteRequest(const FCraftingResult& Result);
	void HandleInventoryChanged();
	bool ValidateCraftingContext(AActor* ContextActor) const;
};
