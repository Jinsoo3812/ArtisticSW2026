#include "Crafting/CraftingComponent.h"

#include "Crafting/CraftingAccessComponent.h"
#include "Crafting/CraftingOutputReceiver.h"
#include "Inventory/InventoryComponent.h"
#include "Item/ItemData.h"
#include "Item/ItemSubsystem.h"

UCraftingComponent::UCraftingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UCraftingComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UInventoryComponent* Inventory = ResolveInventory())
	{
		Inventory->OnInventoryChanged.AddUObject(this, &UCraftingComponent::HandleInventoryChanged);
	}
}

void UCraftingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UInventoryComponent* Inventory = CachedInventory.Get())
	{
		Inventory->OnInventoryChanged.RemoveAll(this);
	}
	Super::EndPlay(EndPlayReason);
}

UInventoryComponent* UCraftingComponent::ResolveInventory() const
{
	if (CachedInventory.IsValid())
	{
		return CachedInventory.Get();
	}
	AActor* Owner = GetOwner();
	UInventoryComponent* Inventory = Owner ? Owner->FindComponentByClass<UInventoryComponent>() : nullptr;
	const_cast<UCraftingComponent*>(this)->CachedInventory = Inventory;
	return Inventory;
}

int32 UCraftingComponent::GetOwnedItemCount(FGameplayTag ItemTag) const
{
	const UInventoryComponent* Inventory = ResolveInventory();
	return Inventory ? Inventory->GetItemCount(ItemTag) : 0;
}

ECraftingAvailability UCraftingComponent::EvaluateAvailability(const FCraftingRecipeRow& Recipe, int32 CraftCount) const
{
	if (!Recipe.bEnabled || Recipe.Ingredients.Num() > ArtisticCrafting::MaxIngredientSlots)
	{
		return ECraftingAvailability::Disabled;
	}
	if (Recipe.RequiredRecipeItemTag.IsValid() && GetOwnedItemCount(Recipe.RequiredRecipeItemTag) <= 0)
	{
		return ECraftingAvailability::MissingRecipe;
	}
	if (Recipe.bConsumeRecipeItem && Recipe.RequiredRecipeItemTag.IsValid()
		&& GetOwnedItemCount(Recipe.RequiredRecipeItemTag) < CraftCount)
	{
		return ECraftingAvailability::MissingIngredients;
	}
	if (CraftCount <= 0)
	{
		return ECraftingAvailability::MissingIngredients;
	}
	for (const FCraftingItemStack& Ingredient : Recipe.Ingredients)
	{
		const int64 Required = static_cast<int64>(Ingredient.Quantity) * CraftCount;
		if (Required > MAX_int32 || GetOwnedItemCount(Ingredient.ItemTag) < Required)
		{
			return ECraftingAvailability::MissingIngredients;
		}
	}
	return ECraftingAvailability::Available;
}

FCraftingListEntry UCraftingComponent::BuildListEntry(FName RecipeId, int32 CraftCount) const
{
	FCraftingListEntry Entry;
	Entry.RecipeId = RecipeId;
	UWorld* World = GetWorld();
	UItemSubsystem* Items = World ? World->GetSubsystem<UItemSubsystem>() : nullptr;
	const FCraftingRecipeRow* Recipe = Items ? Items->FindCraftingRecipe(RecipeId) : nullptr;
	if (!Recipe)
	{
		return Entry;
	}

	Entry.ResultItemTag = Recipe->ResultItemTag;
	Entry.ResultQuantity = Recipe->ResultQuantity;
	Entry.Availability = EvaluateAvailability(*Recipe, CraftCount);
	if (Items)
	{
		Entry.DisplayName = Items->GetItemName(Recipe->ResultItemTag);
		Entry.Icon = Items->GetIcon2D(Recipe->ResultItemTag);
		if (const FItemDefinition* Definition = Items->GetItemDefinition(Recipe->ResultItemTag))
		{
			Entry.CategoryTag = Definition->CategoryTag;
			Entry.RarityTag = Definition->RarityTag;
		}
	}
	return Entry;
}

TArray<FCraftingListEntry> UCraftingComponent::GetCraftableList(const FCraftingListQuery& Query) const
{
	TArray<FCraftingListEntry> Result;
	UWorld* World = GetWorld();
	UItemSubsystem* Items = World ? World->GetSubsystem<UItemSubsystem>() : nullptr;
	if (!Items)
	{
		return Result;
	}

	TArray<FName> RecipeIds;
	Items->GetCraftingRecipeIds(RecipeIds, Query.bIncludeDisabled);
	for (const FName RecipeId : RecipeIds)
	{
		FCraftingListEntry Entry = BuildListEntry(RecipeId, 1);
		if (!Query.bIncludeDisabled && Entry.Availability == ECraftingAvailability::Disabled)
		{
			continue;
		}
		if (!Query.bIncludeLocked && Entry.Availability == ECraftingAvailability::MissingRecipe)
		{
			continue;
		}
		if (!Query.SearchText.IsEmpty())
		{
			const FString Searchable = Entry.DisplayName.ToString() + TEXT(" ") + Entry.ResultItemTag.ToString();
			if (!Searchable.Contains(Query.SearchText, ESearchCase::IgnoreCase))
			{
				continue;
			}
		}
		Result.Add(MoveTemp(Entry));
	}
	return Result;
}

bool UCraftingComponent::GetCraftingDetails(FName RecipeId, int32 CraftCount, FCraftingDetailsView& OutDetails) const
{
	OutDetails = FCraftingDetailsView();
	UWorld* World = GetWorld();
	UItemSubsystem* Items = World ? World->GetSubsystem<UItemSubsystem>() : nullptr;
	const FCraftingRecipeRow* Recipe = Items ? Items->FindCraftingRecipe(RecipeId) : nullptr;
	if (!Recipe)
	{
		return false;
	}
	if (Recipe->Ingredients.Num() > ArtisticCrafting::MaxIngredientSlots)
	{
		return false;
	}

	const int32 SafeCraftCount = FMath::Max(1, CraftCount);
	OutDetails.Header = BuildListEntry(RecipeId, SafeCraftCount);
	OutDetails.RequiredRecipeItemTag = Recipe->RequiredRecipeItemTag;
	OutDetails.Availability = OutDetails.Header.Availability;
	if (Recipe->RequiredRecipeItemTag.IsValid())
	{
		OutDetails.bHasRequiredRecipeItem = true;
		OutDetails.RequiredRecipeItem.ItemTag = Recipe->RequiredRecipeItemTag;
		OutDetails.RequiredRecipeItem.DisplayName = Items->GetItemName(Recipe->RequiredRecipeItemTag);
		OutDetails.RequiredRecipeItem.Icon = Items->GetIcon2D(Recipe->RequiredRecipeItemTag);
		OutDetails.RequiredRecipeItem.OwnedQuantity = GetOwnedItemCount(Recipe->RequiredRecipeItemTag);
		OutDetails.RequiredRecipeItem.RequiredQuantity =
			Recipe->bConsumeRecipeItem ? SafeCraftCount : 1;
		OutDetails.RequiredRecipeItem.bEnough =
			OutDetails.RequiredRecipeItem.OwnedQuantity
			>= OutDetails.RequiredRecipeItem.RequiredQuantity;
	}
	if (OutDetails.Availability == ECraftingAvailability::MissingRecipe)
	{
		OutDetails.bIngredientsVisible = false;
		return true;
	}

	OutDetails.bIngredientsVisible = true;
	int32 MaxCraftable = MAX_int32;
	if (Recipe->bConsumeRecipeItem && Recipe->RequiredRecipeItemTag.IsValid())
	{
		MaxCraftable = GetOwnedItemCount(Recipe->RequiredRecipeItemTag);
	}
	for (const FCraftingItemStack& Ingredient : Recipe->Ingredients)
	{
		FCraftingIngredientView View;
		View.ItemTag = Ingredient.ItemTag;
		View.OwnedQuantity = GetOwnedItemCount(Ingredient.ItemTag);
		const int64 Required = static_cast<int64>(Ingredient.Quantity) * SafeCraftCount;
		View.RequiredQuantity = Required > MAX_int32 ? MAX_int32 : static_cast<int32>(Required);
		View.bEnough = Required <= MAX_int32 && View.OwnedQuantity >= Required;
		View.DisplayName = Items->GetItemName(Ingredient.ItemTag);
		View.Icon = Items->GetIcon2D(Ingredient.ItemTag);
		OutDetails.Ingredients.Add(MoveTemp(View));
		if (Ingredient.Quantity > 0)
		{
			MaxCraftable = FMath::Min(MaxCraftable, GetOwnedItemCount(Ingredient.ItemTag) / Ingredient.Quantity);
		}
	}
	OutDetails.MaxCraftableCount = MaxCraftable == MAX_int32 ? MaxCraftCountPerRequest : MaxCraftable;
	return true;
}

bool UCraftingComponent::ValidateCraftingContext(AActor* ContextActor) const
{
	AActor* Owner = GetOwner();
	if (!Owner || !IsValid(ContextActor))
	{
		return false;
	}
	const UCraftingAccessComponent* Access = ContextActor->FindComponentByClass<UCraftingAccessComponent>();
	if (!Access)
	{
		return false;
	}
	return FVector::DistSquared(Owner->GetActorLocation(), ContextActor->GetActorLocation())
		<= FMath::Square(Access->GetUseDistance());
}

void UCraftingComponent::OpenCraftingScreen(AActor* ContextActor)
{
	AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		if (ValidateCraftingContext(ContextActor))
		{
			CurrentCraftingContext = ContextActor;
			Client_OpenCraftingScreen(ContextActor);
		}
	}
	else
	{
		Server_OpenCraftingScreen(ContextActor);
	}
}

void UCraftingComponent::Server_OpenCraftingScreen_Implementation(AActor* ContextActor)
{
	if (ValidateCraftingContext(ContextActor))
	{
		CurrentCraftingContext = ContextActor;
		Client_OpenCraftingScreen(ContextActor);
	}
}

void UCraftingComponent::Client_OpenCraftingScreen_Implementation(AActor* ContextActor)
{
	CurrentCraftingContext = ContextActor;
	OnCraftingScreenOpened.Broadcast(ContextActor);
}

void UCraftingComponent::CloseCraftingScreen()
{
	AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		CurrentCraftingContext.Reset();
		Client_CloseCraftingScreen();
	}
	else
	{
		Server_CloseCraftingScreen();
	}
}

void UCraftingComponent::Server_CloseCraftingScreen_Implementation()
{
	CurrentCraftingContext.Reset();
	Client_CloseCraftingScreen();
}

void UCraftingComponent::Client_CloseCraftingScreen_Implementation()
{
	CurrentCraftingContext.Reset();
	OnCraftingScreenClosed.Broadcast();
}

bool UCraftingComponent::BuildCosts(const FCraftingRecipeRow& Recipe, int32 CraftCount, TArray<FCraftingItemStack>& OutCosts) const
{
	OutCosts.Reset();
	TMap<FGameplayTag, int64> Totals;
	for (const FCraftingItemStack& Ingredient : Recipe.Ingredients)
	{
		if (!Ingredient.ItemTag.IsValid() || Ingredient.Quantity <= 0)
		{
			return false;
		}
		Totals.FindOrAdd(Ingredient.ItemTag) += static_cast<int64>(Ingredient.Quantity) * CraftCount;
	}
	if (Recipe.bConsumeRecipeItem && Recipe.RequiredRecipeItemTag.IsValid())
	{
		Totals.FindOrAdd(Recipe.RequiredRecipeItemTag) += CraftCount;
	}
	for (const TPair<FGameplayTag, int64>& Pair : Totals)
	{
		if (Pair.Value <= 0 || Pair.Value > MAX_int32)
		{
			return false;
		}
		FCraftingItemStack Stack;
		Stack.ItemTag = Pair.Key;
		Stack.Quantity = static_cast<int32>(Pair.Value);
		OutCosts.Add(Stack);
	}
	return true;
}

void UCraftingComponent::RequestCraft(FCraftingRequest Request)
{
	if (!Request.RequestId.IsValid())
	{
		Request.RequestId = FGuid::NewGuid();
	}
	AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		ProcessCraftRequest(Request);
	}
	else
	{
		Server_RequestCraft(Request);
	}
}

void UCraftingComponent::Server_RequestCraft_Implementation(const FCraftingRequest& Request)
{
	ProcessCraftRequest(Request);
}

void UCraftingComponent::ProcessCraftRequest(const FCraftingRequest& Request)
{
	FCraftingResult Result;
	Result.RequestId = Request.RequestId;
	Result.RecipeId = Request.RecipeId;
	if (!Request.RequestId.IsValid() || ProcessedRequestIds.Contains(Request.RequestId))
	{
		Result.Reason = ECraftingFailureReason::DuplicateRequest;
		CompleteRequest(Result);
		return;
	}
	ProcessedRequestIds.Add(Request.RequestId);

	UWorld* World = GetWorld();
	UItemSubsystem* Items = World ? World->GetSubsystem<UItemSubsystem>() : nullptr;
	const FCraftingRecipeRow* Recipe = Items ? Items->FindCraftingRecipe(Request.RecipeId) : nullptr;
	if (!Recipe)
	{
		Result.Reason = ECraftingFailureReason::InvalidRecipe;
		CompleteRequest(Result);
		return;
	}
	if (Recipe->Ingredients.Num() > ArtisticCrafting::MaxIngredientSlots)
	{
		Result.Reason = ECraftingFailureReason::InvalidRecipe;
		CompleteRequest(Result);
		return;
	}
	Result.ResultItemTag = Recipe->ResultItemTag;
	if (!Recipe->bEnabled)
	{
		Result.Reason = ECraftingFailureReason::RecipeDisabled;
		CompleteRequest(Result);
		return;
	}
	if (Request.CraftCount <= 0 || Request.CraftCount > MaxCraftCountPerRequest)
	{
		Result.Reason = ECraftingFailureReason::InvalidQuantity;
		CompleteRequest(Result);
		return;
	}

	AActor* Owner = GetOwner();
	AActor* ContextActor = CurrentCraftingContext.Get();
	if (!Owner || !IsValid(ContextActor))
	{
		Result.Reason = ECraftingFailureReason::NoActiveContext;
		CompleteRequest(Result);
		return;
	}
	const UCraftingAccessComponent* Access = ContextActor->FindComponentByClass<UCraftingAccessComponent>();
	if (!Access)
	{
		Result.Reason = ECraftingFailureReason::NoActiveContext;
		CompleteRequest(Result);
		return;
	}
	if (FVector::DistSquared(Owner->GetActorLocation(), ContextActor->GetActorLocation()) > FMath::Square(Access->GetUseDistance()))
	{
		Result.Reason = ECraftingFailureReason::OutOfRange;
		CompleteRequest(Result);
		return;
	}

	UInventoryComponent* Inventory = ResolveInventory();
	if (!Inventory)
	{
		Result.Reason = ECraftingFailureReason::InternalError;
		CompleteRequest(Result);
		return;
	}
	if (Recipe->RequiredRecipeItemTag.IsValid() && Inventory->GetItemCount(Recipe->RequiredRecipeItemTag) <= 0)
	{
		Result.Reason = ECraftingFailureReason::MissingRecipe;
		CompleteRequest(Result);
		return;
	}

	TArray<FCraftingItemStack> Costs;
	if (!BuildCosts(*Recipe, Request.CraftCount, Costs))
	{
		Result.Reason = ECraftingFailureReason::InternalError;
		CompleteRequest(Result);
		return;
	}
	for (const FCraftingItemStack& Cost : Costs)
	{
		if (Inventory->GetItemCount(Cost.ItemTag) < Cost.Quantity)
		{
			Result.Reason = ECraftingFailureReason::MissingIngredients;
			CompleteRequest(Result);
			return;
		}
	}

	const int64 ResultTotal64 = static_cast<int64>(Recipe->ResultQuantity) * Request.CraftCount;
	if (ResultTotal64 <= 0 || ResultTotal64 > MAX_int32)
	{
		Result.Reason = ECraftingFailureReason::InvalidQuantity;
		CompleteRequest(Result);
		return;
	}
	FCraftingItemStack CraftedStack;
	CraftedStack.ItemTag = Recipe->ResultItemTag;
	CraftedStack.Quantity = static_cast<int32>(ResultTotal64);

	bool bDelivered = false;
	if (Request.Output.Type == ECraftingOutputType::Inventory)
	{
		bDelivered = Inventory->TryApplyCraftingTransaction(Costs, CraftedStack);
		if (!bDelivered)
		{
			Result.Reason = ECraftingFailureReason::OutputUnavailable;
			CompleteRequest(Result);
			return;
		}
	}
	else
	{
		AActor* Receiver = Request.Output.ReceiverActor;
		if (!IsValid(Receiver) || !Access->IsExternalReceiverAllowed(Receiver) || !Receiver->GetClass()->ImplementsInterface(UCraftingOutputReceiver::StaticClass()))
		{
			Result.Reason = ECraftingFailureReason::OutputUnavailable;
			CompleteRequest(Result);
			return;
		}
		if (!ICraftingOutputReceiver::Execute_CanReceiveCraftedItem(Receiver, CraftedStack.ItemTag, CraftedStack.Quantity, Owner))
		{
			Result.Reason = ECraftingFailureReason::OutputUnavailable;
			CompleteRequest(Result);
			return;
		}
		if (!Inventory->RemoveItemsAtomically(Costs))
		{
			Result.Reason = ECraftingFailureReason::MissingIngredients;
			CompleteRequest(Result);
			return;
		}
		bDelivered = ICraftingOutputReceiver::Execute_ReceiveCraftedItem(Receiver, CraftedStack.ItemTag, CraftedStack.Quantity, Owner);
		if (!bDelivered)
		{
			if (!Inventory->AddItemsAtomically(Costs))
			{
				UE_LOG(LogTemp, Error, TEXT("Crafting rollback failed for %s."), *GetNameSafe(Owner));
			}
			Result.Reason = ECraftingFailureReason::OutputRejected;
			CompleteRequest(Result);
			return;
		}
	}

	Result.DeliveredQuantity = CraftedStack.Quantity;
	Result.Reason = ECraftingFailureReason::Success;
	CompleteRequest(Result);
}

void UCraftingComponent::CompleteRequest(const FCraftingResult& Result)
{
	LastCraftingResult = Result;
	AActor* Owner = GetOwner();
	if (Owner && Owner->GetNetMode() == NM_Standalone)
	{
		OnCraftingResult.Broadcast(Result);
	}
	else
	{
		Client_NotifyCraftingResult(Result);
	}
}

void UCraftingComponent::Client_NotifyCraftingResult_Implementation(const FCraftingResult& Result)
{
	LastCraftingResult = Result;
	OnCraftingResult.Broadcast(Result);
}

void UCraftingComponent::HandleInventoryChanged()
{
	OnCraftingDataChanged.Broadcast();
}
