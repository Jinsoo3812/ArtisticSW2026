#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "PlayerEquipmentComponent.generated.h"

class ABaseItem;
class ABasePlayer;
class UAnimMontage;
class UAnimInstance;
class UWeaponAnimationDataAsset;
struct FWeaponAnimationEntry;

USTRUCT(BlueprintType)
struct FWeaponAnimationDataMapping
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FGameplayTag WeaponTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UWeaponAnimationDataAsset> AnimationData;
};

UENUM(BlueprintType)
enum class EEquipmentState : uint8
{
	None,
	Equipping,
	Equipped,
	Unequipping
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CLASSFEATURE_API UPlayerEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerEquipmentComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Equipment")
	EEquipmentState GetEquipmentState() const { return EquipmentState; }

	UFUNCTION(BlueprintPure, Category = "Equipment")
	bool IsEquipmentTransitioning() const;

	void EquipItemFromSlot(FGameplayTag KeyTag);
	void UseEquippedItem(bool bDestroy = true);
	void HandleEquipmentAttachNotify();
	void OnRepOwnerEquippedItem();

	UFUNCTION(BlueprintPure, Category = "Equipment")
	FGameplayTag GetEquippedItemTag() const;

	UFUNCTION(BlueprintPure, Category = "Equipment")
	bool IsEquippedItemTag(FGameplayTag ItemTag) const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Animation")
	FGameplayTag GetEquippedUpperBodyOverlayTag() const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Animation")
	bool ShouldUseEquippedUpperBodyOverlay() const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Animation")
	int32 GetEquippedUpperBodyOverlayIndex() const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Animation")
	UAnimMontage* GetEquippedCombatIntroMontage() const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Animation")
	float GetEquippedCombatIntroPlayRate() const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Animation")
	UAnimMontage* GetEquippedReloadMontage() const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Animation")
	float GetEquippedReloadPlayRate() const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Animation")
	UAnimMontage* GetEquippedAimCycleMontage() const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Animation")
	TSubclassOf<UAnimInstance> GetEquippedWeaponAnimLayerClass() const;

	const FWeaponAnimationEntry* GetEquippedWeaponAnimationEntry() const;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_EquipmentState, Category = "Equipment")
	EEquipmentState EquipmentState = EEquipmentState::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Animation")
	TObjectPtr<UWeaponAnimationDataAsset> WeaponAnimationData;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Animation")
	TArray<FWeaponAnimationDataMapping> WeaponAnimationDataByTag;

	UPROPERTY(Transient)
	TObjectPtr<ABaseItem> PendingEquipItem;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveEquipmentMontage;

	UPROPERTY(Transient)
	FGameplayTag PendingEquipSlotTag;

	UPROPERTY(Transient)
	TObjectPtr<ABasePlayer> PlayerOwner;

	UFUNCTION()
	void OnRep_EquipmentState();

	UFUNCTION(Server, Reliable)
	void Server_EquipItemFromSlot(FGameplayTag KeyTag);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayEquipmentMontage(ABaseItem* Item, UAnimMontage* Montage, float PlayRate);

	const UWeaponAnimationDataAsset* ResolveWeaponAnimationData(const ABaseItem* Item) const;
	const FWeaponAnimationEntry* ResolveWeaponAnimationEntry(const ABaseItem* Item) const;
	FName ResolveEquipSocketName(const ABaseItem* Item) const;
	FName ResolveItemGripSocketName(const ABaseItem* Item) const;
	FName ResolveStoredSocketName(const ABaseItem* Item) const;
	FGameplayTag ResolveUseKeyTag(const ABaseItem* Item) const;
	bool CanUseEquippedItemAbility(const ABaseItem* Item) const;
	void GrantEquippedItemAbility(ABaseItem* Item);
	void RemoveEquippedItemAbility(ABaseItem* Item);
	void AttachItemToSocket(ABaseItem* Item, FName SocketName) const;
	void StoreCurrentEquippedItem();
	void StartEquipItemFromSlot(int32 SlotIndex);
	void FinalizePendingEquip();
	void CancelPendingEquip();
	void PlayEquipmentMontage(ABaseItem* Item, UAnimMontage* Montage, float PlayRate);
	void OnEquipmentMontageEnded(UAnimMontage* Montage, bool bInterrupted);
};
