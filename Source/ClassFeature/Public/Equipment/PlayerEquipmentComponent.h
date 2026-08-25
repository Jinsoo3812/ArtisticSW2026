#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "PlayerEquipmentComponent.generated.h"

class ABaseItem;
class ABasePlayer;
class ABowItem;
class UAnimMontage;
class UAnimInstance;
class UGameplayEffect;
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

UENUM(BlueprintType)
enum class EEquipmentAttachmentTarget : uint8
{
	Equipped,
	Stored
};

/** Fully resolved socket pair used for one attachment operation. */
USTRUCT(BlueprintType)
struct FResolvedEquipmentAttachment
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment|Attachment")
	FName CharacterSocketName = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment|Attachment")
	FName ItemGripSocketName = NAME_None;

	bool IsValid() const { return !CharacterSocketName.IsNone(); }
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
	bool EquipInventoryWeapon(FGameplayTag ItemTag);
	void UnequipCurrentItem();
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
	UAnimMontage* GetEquippedBasicAttackMontage() const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Animation")
	TArray<FName> GetEquippedBasicAttackComboSections() const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Animation")
	float GetEquippedBasicAttackPlayRate() const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Animation")
	UAnimMontage* GetEquippedAimCycleMontage() const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Animation")
	TSubclassOf<UAnimInstance> GetEquippedWeaponAnimLayerClass() const;

	const FWeaponAnimationEntry* GetEquippedWeaponAnimationEntry() const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Attachment")
	FResolvedEquipmentAttachment GetEquippedAttachmentProfile() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_EquipmentState, Category = "Equipment")
	EEquipmentState EquipmentState = EEquipmentState::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Animation")
	TObjectPtr<UWeaponAnimationDataAsset> WeaponAnimationData;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Animation")
	TArray<FWeaponAnimationDataMapping> WeaponAnimationDataByTag;

	/** Common infinite GE used for an equipped item's Data.StrengthBonus. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Strength")
	TSubclassOf<UGameplayEffect> StrengthEquipmentEffectClass;

	UPROPERTY(Transient)
	TObjectPtr<ABaseItem> PendingEquipItem;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveEquipmentMontage;

	UPROPERTY(Transient)
	FGameplayTag PendingEquipSlotTag;

	UPROPERTY(Transient)
	TObjectPtr<ABasePlayer> PlayerOwner;

	/** Locally reconstructed from equipment attachment; never replicated as a component pointer. */
	TWeakObjectPtr<ABowItem> BoundBowArrowAnchor;

	/** Handles cross-actor replication ordering between EquippedItem and the item's ItemTag. */
	TWeakObjectPtr<ABaseItem> ObservedReplicatedEquippedItem;

	UFUNCTION()
	void OnRep_EquipmentState();

	UFUNCTION(Server, Reliable)
	void Server_EquipItemFromSlot(FGameplayTag KeyTag);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayEquipmentMontage(ABaseItem* Item, UAnimMontage* Montage, float PlayRate);

	const UWeaponAnimationDataAsset* ResolveWeaponAnimationData(const ABaseItem* Item) const;
	const FWeaponAnimationEntry* ResolveWeaponAnimationEntry(const ABaseItem* Item) const;
	FResolvedEquipmentAttachment ResolveAttachmentProfile(const ABaseItem* Item, EEquipmentAttachmentTarget Target) const;
	FName ResolveCharacterSocketName(const ABaseItem* Item, EEquipmentAttachmentTarget Target) const;
	bool IsCharacterSocketValid(FName SocketName) const;
	FGameplayTag ResolveUseKeyTag(const ABaseItem* Item) const;
	bool CanUseEquippedItemAbility(const ABaseItem* Item) const;
	void CancelActiveWeaponAbilities() const;
	void GrantEquippedItemAbility(ABaseItem* Item);
	void RemoveEquippedItemAbility(ABaseItem* Item);
	bool AttachItem(ABaseItem* Item, EEquipmentAttachmentTarget Target);
	bool CompleteItemAttachment(ABaseItem* Item, EEquipmentAttachmentTarget Target, bool bAttached);
	void ClearBowArrowAnchor(ABowItem* ExpectedBow = nullptr);
	void ObserveReplicatedEquippedItem(ABaseItem* Item);
	void HandleReplicatedEquippedItemInitialized(ABaseItem* Item);
	bool IsItemOwnedByItemSlot(const ABaseItem* Item) const;
	void StoreCurrentEquippedItem();
	void StartEquipItemFromSlot(int32 SlotIndex);
	void StartEquipItem(ABaseItem* Item, FGameplayTag SourceSlotTag);
	void FinalizePendingEquip();
	void CancelPendingEquip();
	void PlayEquipmentMontage(ABaseItem* Item, UAnimMontage* Montage, float PlayRate);
	void OnEquipmentMontageEnded(UAnimMontage* Montage, bool bInterrupted);
};
