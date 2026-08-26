#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Upgrade/ShipUpgradeTypes.h"
#include "ShipUpgradeComponent.generated.h"

class UShipUpgradeTreeDataAsset;
class IShipUpgradeInventoryProvider;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FShipUpgradeDataReadySignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FShipUpgradeDataChangedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FShipUpgradeNodeStateChangedSignature, FName, NodeId, EShipUpgradeNodeState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FShipUpgradeActivationResultSignature, FName, NodeId, EShipUpgradeActivationResult, Result, FText, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FShipUpgradeStatsChangedSignature, FShipStatSnapshot, NewStats);

UCLASS(ClassGroup = (Ship), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class WATERANDSHIP_API UShipUpgradeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShipUpgradeComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship Upgrade")
	TObjectPtr<UShipUpgradeTreeDataAsset> UpgradeTree;

	/** Used by UI before a combat ship exists. Keep it synchronized with PlayerShip's base DT row. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Ship Upgrade")
	FShipStatSnapshot PreviewBaseStats;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship Upgrade|Persistence")
	bool bAutoLoadAndSaveLocalProgress = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship Upgrade|Persistence")
	FString SaveSlotName = TEXT("ShipUpgradeProgress");

	/**
	 * Development-only material bypass for testing this component.
	 * It is forcibly ignored in Shipping builds and does not affect crafting or any other inventory consumer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship Upgrade|Testing", meta = (DevelopmentOnly))
	bool bIgnoreMaterialCostsForTesting = false;

	UPROPERTY(BlueprintAssignable, Category = "Ship Upgrade|Events")
	FShipUpgradeDataReadySignature OnUpgradeDataReady;

	/** Fired when inventory quantities or activation state change. Re-query affected Node Views. */
	UPROPERTY(BlueprintAssignable, Category = "Ship Upgrade|Events")
	FShipUpgradeDataChangedSignature OnUpgradeDataChanged;

	UPROPERTY(BlueprintAssignable, Category = "Ship Upgrade|Events")
	FShipUpgradeNodeStateChangedSignature OnNodeStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Ship Upgrade|Events")
	FShipUpgradeActivationResultSignature OnNodeActivationResult;

	UPROPERTY(BlueprintAssignable, Category = "Ship Upgrade|Events")
	FShipUpgradeStatsChangedSignature OnShipStatsChanged;

	UFUNCTION(BlueprintPure, Category = "Ship Upgrade|UI")
	TArray<FShipUpgradeNodeView> GetAllNodeViews() const;

	UFUNCTION(BlueprintPure, Category = "Ship Upgrade|UI")
	bool GetNodeView(FName NodeId, FShipUpgradeNodeView& OutView) const;

	UFUNCTION(BlueprintPure, Category = "Ship Upgrade|UI")
	EShipUpgradeNodeState GetNodeState(FName NodeId) const;

	UFUNCTION(BlueprintPure, Category = "Ship Upgrade|UI")
	bool IsNodeActive(FName NodeId) const;

	UFUNCTION(BlueprintPure, Category = "Ship Upgrade|UI")
	bool CanActivateNode(FName NodeId, FText& OutReason) const;

	UFUNCTION(BlueprintPure, Category = "Ship Upgrade|UI")
	FShipStatSnapshot GetCurrentShipStats() const;

	UFUNCTION(BlueprintPure, Category = "Ship Upgrade|UI")
	bool GetStatsAfterActivating(FName NodeId, FShipStatSnapshot& OutPreviewStats) const;

	UFUNCTION(BlueprintPure, Category = "Ship Upgrade|UI")
	TArray<FShipStatChangeView> GetNodeStatChanges(FName NodeId) const;

	UFUNCTION(BlueprintPure, Category = "Ship Upgrade|UI")
	TArray<FShipUpgradeMaterialView> GetNodeMaterialCosts(FName NodeId) const;

	UFUNCTION(BlueprintPure, Category = "Ship Upgrade|UI")
	bool HasRequiredMaterials(FName NodeId, FText& OutReason) const;

	/** Useful when an external inventory UI changed data before automatic binding completed. */
	UFUNCTION(BlueprintCallable, Category = "Ship Upgrade|UI")
	void RefreshUpgradeData();

	UFUNCTION(BlueprintCallable, Category = "Ship Upgrade|UI")
	void RequestActivateNode(FName NodeId);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ship Upgrade|Testing", meta = (DevelopmentOnly))
	void SetIgnoreMaterialCostsForTesting(bool bInIgnore);

	UFUNCTION(BlueprintPure, Category = "Ship Upgrade|Testing", meta = (DevelopmentOnly))
	bool IsIgnoringMaterialCostsForTesting() const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ship Upgrade")
	void SetPreviewBaseStats(const FShipStatSnapshot& InBaseStats);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ship Upgrade|Persistence")
	bool LoadProgress();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ship Upgrade|Persistence")
	bool SaveProgress() const;

	const TArray<FName>& GetActiveNodeIds() const { return ActiveNodeIds; }
	void RestoreActiveNodeIds(const TArray<FName>& InActiveNodeIds);
	void ConfigureForUseCase(UShipUpgradeTreeDataAsset* InTree, const FShipStatSnapshot& InBaseStats, bool bEnablePersistence);
	EShipUpgradeActivationResult ActivateNodeForUseCase(FName NodeId);

protected:
	UPROPERTY(ReplicatedUsing = OnRep_ActiveNodeIds)
	TArray<FName> ActiveNodeIds;

	UFUNCTION()
	void OnRep_ActiveNodeIds(const TArray<FName>& PreviousNodeIds);

	UFUNCTION(Server, Reliable)
	void ServerRequestActivateNode(FName NodeId);

	UFUNCTION(Client, Reliable)
	void ClientReceiveActivationResult(FName NodeId, EShipUpgradeActivationResult Result, const FText& Message);

private:
	EShipUpgradeActivationResult ActivateNodeInternal(FName NodeId, bool bPersist);
	FText GetActivationMessage(FName NodeId, EShipUpgradeActivationResult Result) const;
	FString GetResolvedSaveSlotName() const;
	void BroadcastStateDiff(const TArray<FName>& PreviousNodeIds);
	IShipUpgradeInventoryProvider* ResolveInventoryProvider() const;
	bool BuildAggregatedCosts(const FShipUpgradeNodeDefinition& Node, TArray<FCraftingItemStack>& OutCosts) const;
	bool ShouldIgnoreMaterialCostsForTesting() const;
	void HandleInventoryChanged();

	UPROPERTY(Transient)
	TWeakObjectPtr<UActorComponent> BoundInventoryComponent;
};
