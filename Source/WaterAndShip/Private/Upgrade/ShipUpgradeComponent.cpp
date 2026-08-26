#include "Upgrade/ShipUpgradeComponent.h"

#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Item/ItemSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Upgrade/ShipUpgradeInventoryProvider.h"
#include "Upgrade/ShipUpgradeSaveGame.h"
#include "Upgrade/ShipUpgradeTreeDataAsset.h"

UShipUpgradeComponent::UShipUpgradeComponent()
{
	SetIsReplicatedByDefault(true);
}

void UShipUpgradeComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!UpgradeTree)
	{
		UpgradeTree = LoadObject<UShipUpgradeTreeDataAsset>(
			nullptr,
			TEXT("/Game/New/Ship/Upgrade/DA_ShipUpgradeTree.DA_ShipUpgradeTree"));
	}
	if (UpgradeTree)
	{
		PreviewBaseStats = UpgradeTree->PreviewBaseStats;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ShipUpgradeComponent: No upgrade tree is configured on %s."), *GetNameSafe(GetOwner()));
	}
	if (GetOwner() && GetOwner()->HasAuthority() && bAutoLoadAndSaveLocalProgress && GetNetMode() != NM_DedicatedServer)
	{
		LoadProgress();
	}
	OnUpgradeDataReady.Broadcast();
}

void UShipUpgradeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UActorComponent* InventoryComponent = BoundInventoryComponent.Get())
	{
		if (IShipUpgradeInventoryProvider* Provider = Cast<IShipUpgradeInventoryProvider>(InventoryComponent))
		{
			Provider->GetShipUpgradeInventoryChangedDelegate().RemoveAll(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UShipUpgradeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UShipUpgradeComponent, ActiveNodeIds, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION(UShipUpgradeComponent, PreviewBaseStats, COND_OwnerOnly);
}

TArray<FShipUpgradeNodeView> UShipUpgradeComponent::GetAllNodeViews() const
{
	TArray<FShipUpgradeNodeView> Views;
	if (!UpgradeTree) return Views;
	Views.Reserve(UpgradeTree->Nodes.Num());
	for (const FShipUpgradeNodeDefinition& Node : UpgradeTree->Nodes)
	{
		FShipUpgradeNodeView View;
		if (GetNodeView(Node.NodeId, View)) Views.Add(MoveTemp(View));
	}
	return Views;
}

bool UShipUpgradeComponent::GetNodeView(FName NodeId, FShipUpgradeNodeView& OutView) const
{
	if (!UpgradeTree) return false;
	const FShipUpgradeNodeDefinition* Node = UpgradeTree->FindNode(NodeId);
	if (!Node) return false;
	OutView.NodeId = Node->NodeId;
	OutView.DisplayName = Node->DisplayName;
	OutView.Description = Node->Description;
	OutView.Icon = Node->Icon;
	OutView.PreviewType = Node->PreviewType;
	OutView.PreviewActorClass = Node->PreviewActorClass;
	OutView.ActivatedShipActorClass = Node->ActivatedShipActorClass;
	OutView.ActivatedCannonActorClass = Node->ActivatedCannonActorClass;
	OutView.VisualPriority = Node->VisualPriority;
	OutView.CameraPreset = Node->CameraPreset;
	OutView.GraphPosition = Node->GraphPosition;
	OutView.PrerequisiteNodeIds = Node->PrerequisiteNodeIds;
	OutView.State = GetNodeState(NodeId);
	OutView.StatChanges = GetNodeStatChanges(NodeId);
	OutView.MaterialCosts = GetNodeMaterialCosts(NodeId);
	OutView.bHasEnoughMaterials = true;
	for (const FShipUpgradeMaterialView& Cost : OutView.MaterialCosts)
	{
		OutView.bHasEnoughMaterials &= Cost.bEnough;
	}
	if (OutView.State != EShipUpgradeNodeState::Active)
	{
		CanActivateNode(NodeId, OutView.UnavailableReason);
	}
	return true;
}

EShipUpgradeNodeState UShipUpgradeComponent::GetNodeState(FName NodeId) const
{
	if (IsNodeActive(NodeId)) return EShipUpgradeNodeState::Active;
	if (!UpgradeTree) return EShipUpgradeNodeState::Locked;
	const FShipUpgradeNodeDefinition* Node = UpgradeTree->FindNode(NodeId);
	if (!Node) return EShipUpgradeNodeState::Locked;
	for (FName Prerequisite : Node->PrerequisiteNodeIds)
	{
		if (!IsNodeActive(Prerequisite)) return EShipUpgradeNodeState::Locked;
	}
	return EShipUpgradeNodeState::Available;
}

bool UShipUpgradeComponent::IsNodeActive(FName NodeId) const
{
	return ActiveNodeIds.Contains(NodeId);
}

bool UShipUpgradeComponent::CanActivateNode(FName NodeId, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (!UpgradeTree)
	{
		OutReason = NSLOCTEXT("ShipUpgrade", "TreeNotConfigured", "강화 트리가 설정되지 않았습니다.");
		return false;
	}
	const FShipUpgradeNodeDefinition* Node = UpgradeTree->FindNode(NodeId);
	if (!Node)
	{
		OutReason = NSLOCTEXT("ShipUpgrade", "UnknownNode", "존재하지 않는 강화 노드입니다.");
		return false;
	}
	if (IsNodeActive(NodeId))
	{
		OutReason = NSLOCTEXT("ShipUpgrade", "AlreadyActive", "이미 활성화된 노드입니다.");
		return false;
	}
	for (FName Prerequisite : Node->PrerequisiteNodeIds)
	{
		if (!IsNodeActive(Prerequisite))
		{
			OutReason = FText::Format(NSLOCTEXT("ShipUpgrade", "RequiresNode", "선행 노드 {0} 활성화가 필요합니다."), FText::FromName(Prerequisite));
			return false;
		}
	}
	return HasRequiredMaterials(NodeId, OutReason);
}

FShipStatSnapshot UShipUpgradeComponent::GetCurrentShipStats() const
{
	return UpgradeTree ? FShipUpgradeCalculator::Calculate(PreviewBaseStats, UpgradeTree->Nodes, ActiveNodeIds) : PreviewBaseStats;
}

bool UShipUpgradeComponent::GetStatsAfterActivating(FName NodeId, FShipStatSnapshot& OutPreviewStats) const
{
	FText Reason;
	if (!UpgradeTree || (!CanActivateNode(NodeId, Reason) && !IsNodeActive(NodeId))) return false;
	TArray<FName> PreviewIds = ActiveNodeIds;
	PreviewIds.AddUnique(NodeId);
	OutPreviewStats = FShipUpgradeCalculator::Calculate(PreviewBaseStats, UpgradeTree->Nodes, PreviewIds);
	return true;
}

TArray<FShipStatChangeView> UShipUpgradeComponent::GetNodeStatChanges(FName NodeId) const
{
	TArray<FShipStatChangeView> Changes;
	if (!UpgradeTree || !UpgradeTree->FindNode(NodeId)) return Changes;
	TArray<FName> BeforeIds = ActiveNodeIds;
	BeforeIds.Remove(NodeId);
	TArray<FName> AfterIds = BeforeIds;
	AfterIds.Add(NodeId);
	const FShipStatSnapshot Before = FShipUpgradeCalculator::Calculate(PreviewBaseStats, UpgradeTree->Nodes, BeforeIds);
	const FShipStatSnapshot After = FShipUpgradeCalculator::Calculate(PreviewBaseStats, UpgradeTree->Nodes, AfterIds);
	for (uint8 Index = 0; Index <= static_cast<uint8>(EShipStatType::TurnSpeed); ++Index)
	{
		const EShipStatType StatType = static_cast<EShipStatType>(Index);
		const float BeforeValue = FShipUpgradeCalculator::GetStatValue(Before, StatType);
		const float AfterValue = FShipUpgradeCalculator::GetStatValue(After, StatType);
		if (FMath::IsNearlyEqual(BeforeValue, AfterValue)) continue;
		FShipStatChangeView& Change = Changes.AddDefaulted_GetRef();
		Change.StatType = StatType;
		Change.DisplayName = FShipUpgradeCalculator::GetStatDisplayName(StatType);
		Change.BeforeValue = BeforeValue;
		Change.AfterValue = AfterValue;
		Change.DeltaValue = AfterValue - BeforeValue;
		Change.Unit = FShipUpgradeCalculator::GetStatUnit(StatType);
		const bool bPositiveDelta = Change.DeltaValue > 0.0f;
		Change.bImprovesStat = bPositiveDelta == FShipUpgradeCalculator::IsPositiveDeltaBeneficial(StatType);
		Change.FormattedText = FText::Format(
			NSLOCTEXT("ShipUpgrade", "StatChangeFormat", "{0}: {1}{2} → {3}{2} ({4}{5}{2})"),
			Change.DisplayName,
			FText::AsNumber(Change.BeforeValue),
			Change.Unit,
			FText::AsNumber(Change.AfterValue),
			Change.DeltaValue > 0.0f ? FText::FromString(TEXT("+")) : FText::GetEmpty(),
			FText::AsNumber(Change.DeltaValue));
	}
	return Changes;
}

TArray<FShipUpgradeMaterialView> UShipUpgradeComponent::GetNodeMaterialCosts(FName NodeId) const
{
	TArray<FShipUpgradeMaterialView> Views;
	if (!UpgradeTree) return Views;
	const FShipUpgradeNodeDefinition* Node = UpgradeTree->FindNode(NodeId);
	if (!Node) return Views;
	TArray<FCraftingItemStack> Costs;
	if (!BuildAggregatedCosts(*Node, Costs)) return Views;

	const bool bIgnoreMaterialCosts = ShouldIgnoreMaterialCostsForTesting();
	IShipUpgradeInventoryProvider* Provider = ResolveInventoryProvider();
	UItemSubsystem* Items = GetWorld() ? GetWorld()->GetSubsystem<UItemSubsystem>() : nullptr;
	for (const FCraftingItemStack& Cost : Costs)
	{
		FShipUpgradeMaterialView& View = Views.AddDefaulted_GetRef();
		View.ItemTag = Cost.ItemTag;
		View.RequiredQuantity = Cost.Quantity;
		View.OwnedQuantity = Provider ? Provider->GetShipUpgradeItemCount(Cost.ItemTag) : 0;
		if (bIgnoreMaterialCosts)
		{
			View.OwnedQuantity = FMath::Max(View.OwnedQuantity, View.RequiredQuantity);
		}
		View.bEnough = bIgnoreMaterialCosts || View.OwnedQuantity >= View.RequiredQuantity;
		if (Items)
		{
			View.DisplayName = Items->GetItemName(Cost.ItemTag);
			View.Icon = Items->GetIcon2D(Cost.ItemTag);
		}
	}
	return Views;
}

bool UShipUpgradeComponent::HasRequiredMaterials(FName NodeId, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (!UpgradeTree)
	{
		OutReason = NSLOCTEXT("ShipUpgrade", "CostTreeMissing", "강화 트리가 설정되지 않았습니다.");
		return false;
	}
	const FShipUpgradeNodeDefinition* Node = UpgradeTree->FindNode(NodeId);
	if (!Node)
	{
		OutReason = NSLOCTEXT("ShipUpgrade", "CostNodeMissing", "존재하지 않는 강화 노드입니다.");
		return false;
	}
	TArray<FCraftingItemStack> Costs;
	if (!BuildAggregatedCosts(*Node, Costs))
	{
		OutReason = NSLOCTEXT("ShipUpgrade", "InvalidMaterialCost", "노드의 필요 재료 설정이 잘못되었습니다.");
		return false;
	}
	if (Costs.IsEmpty()) return true;
	if (ShouldIgnoreMaterialCostsForTesting()) return true;
	IShipUpgradeInventoryProvider* Provider = ResolveInventoryProvider();
	if (!Provider)
	{
		OutReason = NSLOCTEXT("ShipUpgrade", "InventoryUnavailable", "플레이어 인벤토리를 찾을 수 없습니다.");
		return false;
	}
	for (const FCraftingItemStack& Cost : Costs)
	{
		const int32 Owned = Provider->GetShipUpgradeItemCount(Cost.ItemTag);
		if (Owned < Cost.Quantity)
		{
			OutReason = FText::Format(
				NSLOCTEXT("ShipUpgrade", "MissingMaterial", "필요 재료가 부족합니다: {0} ({1}/{2})"),
				FText::FromString(Cost.ItemTag.ToString()),
				FText::AsNumber(Owned),
				FText::AsNumber(Cost.Quantity));
			return false;
		}
	}
	return true;
}

void UShipUpgradeComponent::RefreshUpgradeData()
{
	ResolveInventoryProvider();
	OnUpgradeDataChanged.Broadcast();
}

void UShipUpgradeComponent::RequestActivateNode(FName NodeId)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		const EShipUpgradeActivationResult Result = ActivateNodeInternal(NodeId, true);
		ClientReceiveActivationResult(NodeId, Result, GetActivationMessage(NodeId, Result));
	}
	else
	{
		ServerRequestActivateNode(NodeId);
	}
}

void UShipUpgradeComponent::SetIgnoreMaterialCostsForTesting(bool bInIgnore)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;
#if UE_BUILD_SHIPPING
	bInIgnore = false;
#endif
	if (bIgnoreMaterialCostsForTesting == bInIgnore) return;
	bIgnoreMaterialCostsForTesting = bInIgnore;
	OnUpgradeDataChanged.Broadcast();
}

bool UShipUpgradeComponent::IsIgnoringMaterialCostsForTesting() const
{
	return ShouldIgnoreMaterialCostsForTesting();
}

void UShipUpgradeComponent::SetPreviewBaseStats(const FShipStatSnapshot& InBaseStats)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;
	PreviewBaseStats = InBaseStats;
	OnShipStatsChanged.Broadcast(GetCurrentShipStats());
}

bool UShipUpgradeComponent::LoadProgress()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return false;
	const FString Slot = GetResolvedSaveSlotName();
	if (!UGameplayStatics::DoesSaveGameExist(Slot, 0)) return true;
	const UShipUpgradeSaveGame* Save = Cast<UShipUpgradeSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	if (!Save) return false;
	const TArray<FName> Previous = ActiveNodeIds;
	ActiveNodeIds.Reset();
	for (FName NodeId : Save->ActiveNodeIds)
	{
		if (UpgradeTree && UpgradeTree->FindNode(NodeId)) ActiveNodeIds.AddUnique(NodeId);
	}
	BroadcastStateDiff(Previous);
	return true;
}

bool UShipUpgradeComponent::SaveProgress() const
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return false;
	UShipUpgradeSaveGame* Save = Cast<UShipUpgradeSaveGame>(UGameplayStatics::CreateSaveGameObject(UShipUpgradeSaveGame::StaticClass()));
	if (!Save) return false;
	Save->ActiveNodeIds = ActiveNodeIds;
	return UGameplayStatics::SaveGameToSlot(Save, GetResolvedSaveSlotName(), 0);
}

void UShipUpgradeComponent::RestoreActiveNodeIds(const TArray<FName>& InActiveNodeIds)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	const TArray<FName> Previous = ActiveNodeIds;
	ActiveNodeIds.Reset();
	for (const FName NodeId : InActiveNodeIds)
	{
		if (!UpgradeTree || UpgradeTree->FindNode(NodeId)) ActiveNodeIds.AddUnique(NodeId);
	}
	BroadcastStateDiff(Previous);
}

void UShipUpgradeComponent::ConfigureForUseCase(UShipUpgradeTreeDataAsset* InTree, const FShipStatSnapshot& InBaseStats, bool bEnablePersistence)
{
	UpgradeTree = InTree;
	PreviewBaseStats = InBaseStats;
	bAutoLoadAndSaveLocalProgress = bEnablePersistence;
}

EShipUpgradeActivationResult UShipUpgradeComponent::ActivateNodeForUseCase(FName NodeId)
{
	return ActivateNodeInternal(NodeId, false);
}

void UShipUpgradeComponent::OnRep_ActiveNodeIds(const TArray<FName>& PreviousNodeIds)
{
	BroadcastStateDiff(PreviousNodeIds);
}

void UShipUpgradeComponent::ServerRequestActivateNode_Implementation(FName NodeId)
{
	const EShipUpgradeActivationResult Result = ActivateNodeInternal(NodeId, true);
	ClientReceiveActivationResult(NodeId, Result, GetActivationMessage(NodeId, Result));
}

void UShipUpgradeComponent::ClientReceiveActivationResult_Implementation(FName NodeId, EShipUpgradeActivationResult Result, const FText& Message)
{
	OnNodeActivationResult.Broadcast(NodeId, Result, Message);
}

EShipUpgradeActivationResult UShipUpgradeComponent::ActivateNodeInternal(FName NodeId, bool bPersist)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return EShipUpgradeActivationResult::NotAuthority;
	if (!UpgradeTree) return EShipUpgradeActivationResult::NotConfigured;
	if (!UpgradeTree->FindNode(NodeId)) return EShipUpgradeActivationResult::UnknownNode;
	if (IsNodeActive(NodeId)) return EShipUpgradeActivationResult::AlreadyActive;
	if (GetNodeState(NodeId) == EShipUpgradeNodeState::Locked) return EShipUpgradeActivationResult::MissingPrerequisite;
	const FShipUpgradeNodeDefinition* Node = UpgradeTree->FindNode(NodeId);
	TArray<FCraftingItemStack> Costs;
	if (!Node || !BuildAggregatedCosts(*Node, Costs)) return EShipUpgradeActivationResult::InvalidCost;
	const bool bConsumeMaterialCosts = !Costs.IsEmpty() && !ShouldIgnoreMaterialCostsForTesting();
	IShipUpgradeInventoryProvider* Provider = bConsumeMaterialCosts ? ResolveInventoryProvider() : nullptr;
	if (bConsumeMaterialCosts && (!Provider || !Provider->RemoveShipUpgradeItemsAtomically(Costs)))
	{
		return EShipUpgradeActivationResult::MissingMaterials;
	}
	const TArray<FName> Previous = ActiveNodeIds;
	ActiveNodeIds.Add(NodeId);
	if (bPersist && bAutoLoadAndSaveLocalProgress && GetNetMode() != NM_DedicatedServer && !SaveProgress())
	{
		ActiveNodeIds = Previous;
		if (Provider && !Provider->AddShipUpgradeItemsAtomically(Costs))
		{
			UE_LOG(LogTemp, Error, TEXT("Ship upgrade material rollback failed for %s."), *GetNameSafe(GetOwner()));
		}
		return EShipUpgradeActivationResult::SaveFailed;
	}
	BroadcastStateDiff(Previous);
	return EShipUpgradeActivationResult::Success;
}

FText UShipUpgradeComponent::GetActivationMessage(FName NodeId, EShipUpgradeActivationResult Result) const
{
	switch (Result)
	{
	case EShipUpgradeActivationResult::Success: return NSLOCTEXT("ShipUpgrade", "ActivationSuccess", "노드가 활성화되었습니다.");
	case EShipUpgradeActivationResult::AlreadyActive: return NSLOCTEXT("ShipUpgrade", "ActivationAlready", "이미 활성화된 노드입니다.");
	case EShipUpgradeActivationResult::UnknownNode: return NSLOCTEXT("ShipUpgrade", "ActivationUnknown", "존재하지 않는 노드입니다.");
	case EShipUpgradeActivationResult::MissingPrerequisite: { FText Reason; CanActivateNode(NodeId, Reason); return Reason; }
	case EShipUpgradeActivationResult::NotAuthority: return NSLOCTEXT("ShipUpgrade", "ActivationAuthority", "서버 권한이 필요합니다.");
	case EShipUpgradeActivationResult::SaveFailed: return NSLOCTEXT("ShipUpgrade", "ActivationSaveFailed", "강화 상태 저장에 실패했습니다.");
	case EShipUpgradeActivationResult::MissingMaterials: return NSLOCTEXT("ShipUpgrade", "ActivationMissingMaterials", "활성화에 필요한 인벤토리 재료가 부족합니다.");
	case EShipUpgradeActivationResult::InvalidCost: return NSLOCTEXT("ShipUpgrade", "ActivationInvalidCost", "노드의 필요 재료 설정이 잘못되었습니다.");
	default: return NSLOCTEXT("ShipUpgrade", "ActivationNotConfigured", "강화 시스템이 설정되지 않았습니다.");
	}
}

FString UShipUpgradeComponent::GetResolvedSaveSlotName() const
{
	const APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	return PlayerState ? FString::Printf(TEXT("%s_%d"), *SaveSlotName, PlayerState->GetPlayerId()) : SaveSlotName;
}

void UShipUpgradeComponent::BroadcastStateDiff(const TArray<FName>& PreviousNodeIds)
{
	TSet<FName> ChangedIds;
	for (FName Id : PreviousNodeIds) ChangedIds.Add(Id);
	for (FName Id : ActiveNodeIds) ChangedIds.Add(Id);
	for (FName Id : ChangedIds)
	{
		if (PreviousNodeIds.Contains(Id) != ActiveNodeIds.Contains(Id)) OnNodeStateChanged.Broadcast(Id, GetNodeState(Id));
	}
	OnShipStatsChanged.Broadcast(GetCurrentShipStats());
	OnUpgradeDataChanged.Broadcast();
}

IShipUpgradeInventoryProvider* UShipUpgradeComponent::ResolveInventoryProvider() const
{
	auto FindProvider = [](AActor* Actor, UActorComponent*& OutComponent) -> IShipUpgradeInventoryProvider*
	{
		if (!Actor) return nullptr;
		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (IShipUpgradeInventoryProvider* Provider = Cast<IShipUpgradeInventoryProvider>(Component))
			{
				OutComponent = Component;
				return Provider;
			}
		}
		return nullptr;
	};

	UActorComponent* ProviderComponent = nullptr;
	IShipUpgradeInventoryProvider* Provider = FindProvider(GetOwner(), ProviderComponent);
	if (!Provider)
	{
		const APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
		Provider = FindProvider(PlayerState ? PlayerState->GetPawn() : nullptr, ProviderComponent);
	}

	UShipUpgradeComponent* MutableThis = const_cast<UShipUpgradeComponent*>(this);
	if (Provider && MutableThis->BoundInventoryComponent.Get() != ProviderComponent)
	{
		if (UActorComponent* PreviousComponent = MutableThis->BoundInventoryComponent.Get())
		{
			if (IShipUpgradeInventoryProvider* PreviousProvider = Cast<IShipUpgradeInventoryProvider>(PreviousComponent))
			{
				PreviousProvider->GetShipUpgradeInventoryChangedDelegate().RemoveAll(MutableThis);
			}
		}
		Provider->GetShipUpgradeInventoryChangedDelegate().AddUObject(MutableThis, &UShipUpgradeComponent::HandleInventoryChanged);
		MutableThis->BoundInventoryComponent = ProviderComponent;
	}
	return Provider;
}

bool UShipUpgradeComponent::BuildAggregatedCosts(const FShipUpgradeNodeDefinition& Node, TArray<FCraftingItemStack>& OutCosts) const
{
	OutCosts.Reset();
	TMap<FGameplayTag, int64> Totals;
	for (const FCraftingItemStack& Cost : Node.ActivationCosts)
	{
		if (!Cost.ItemTag.IsValid() || Cost.Quantity <= 0) return false;
		Totals.FindOrAdd(Cost.ItemTag) += Cost.Quantity;
	}
	for (const TPair<FGameplayTag, int64>& Pair : Totals)
	{
		if (Pair.Value <= 0 || Pair.Value > MAX_int32) return false;
		FCraftingItemStack& Cost = OutCosts.AddDefaulted_GetRef();
		Cost.ItemTag = Pair.Key;
		Cost.Quantity = static_cast<int32>(Pair.Value);
	}
	OutCosts.Sort([](const FCraftingItemStack& A, const FCraftingItemStack& B)
	{
		return A.ItemTag.ToString() < B.ItemTag.ToString();
	});
	return true;
}

bool UShipUpgradeComponent::ShouldIgnoreMaterialCostsForTesting() const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return bIgnoreMaterialCostsForTesting;
#endif
}

void UShipUpgradeComponent::HandleInventoryChanged()
{
	OnUpgradeDataChanged.Broadcast();
}
