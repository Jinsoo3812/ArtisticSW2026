#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffectTypes.h"
#include "PlayerEquipmentComponent.generated.h"

class ABaseItem;
class ABasePlayer;
class ABowItem;
class UAnimMontage;
class UAnimInstance;
class UAnimSequenceBase;
class UGameplayEffect;
class UAbilitySystemComponent;
class UWeaponAnimationDataAsset;
struct FWeaponAnimationEntry;

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
	friend class FWeaponEquipmentLifecycleTest;

public:
	UPlayerEquipmentComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Equipment")
	EEquipmentState GetEquipmentState() const { return EquipmentState; }

	UFUNCTION(BlueprintPure, Category = "Equipment")
	bool IsEquipmentTransitioning() const;

	bool EquipInventoryItem(FGameplayTag ItemTag);
	void UnequipCurrentItem();
	void UseEquippedItem(bool bDestroy = true);
	void HandleEquipmentAttachNotify();
	void OnRepOwnerEquippedItem();
	void OnEquippedItemInitialized(ABaseItem* Item);

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

	UFUNCTION(BlueprintPure, Category = "Equipment|Preview")
	UAnimSequenceBase* GetEquippedPreviewIdleAnimation() const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Preview")
	float GetEquippedPreviewIdlePlayRate() const;
	UAnimSequenceBase* GetPreviewIdleAnimationForItem(const ABaseItem* Item) const;
	float GetPreviewIdlePlayRateForItem(const ABaseItem* Item) const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Animation")
	UAnimMontage* GetEquippedAimCycleMontage() const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Animation")
	TSubclassOf<UAnimInstance> GetEquippedWeaponAnimLayerClass() const;

	const FWeaponAnimationEntry* GetEquippedWeaponAnimationEntry() const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Attachment")
	FResolvedEquipmentAttachment GetEquippedAttachmentProfile() const;
	FResolvedEquipmentAttachment GetEquippedAttachmentProfileForItem(const ABaseItem* Item) const;
	FResolvedEquipmentAttachment GetPreviewAttachmentProfileForItem(const ABaseItem* Item) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_EquipmentState, Category = "Equipment")
	EEquipmentState EquipmentState = EEquipmentState::None;

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

	/** Locally resolved presentation/spawn anchor; the actor relationship itself already replicates. */
	UPROPERTY(Transient)
	TWeakObjectPtr<ABowItem> BoundBowArrowAnchor;
	TWeakObjectPtr<ABaseItem> PendingPresentationItem;

	UFUNCTION()
	void OnRep_EquipmentState();

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
	bool GrantEquippedItemAbility(ABaseItem* Item);
	bool ValidateWeapon(ABaseItem* Item) const;
	UFUNCTION()
	void OnGrantedItemDestroyed(AActor* Item);
	UFUNCTION()
	void OnGrantedItemEndPlay(AActor* Item, EEndPlayReason::Type Reason);
	void OnOwnerDead(FGameplayTag Tag, int32 Count);
	void BindOwnerAbilitySystem();
	struct FEquipmentGrant
	{
		TWeakObjectPtr<UAbilitySystemComponent> ASC;
		TArray<FGameplayAbilitySpecHandle> Abilities;
		TArray<FActiveGameplayEffectHandle> Effects;
	};
	TMap<TWeakObjectPtr<ABaseItem>, FEquipmentGrant> ItemGrants;
	TWeakObjectPtr<UAbilitySystemComponent> DeathASC;
	FDelegateHandle DeathDelegate;
	void RemoveEquippedItemAbility(ABaseItem* Item);
	bool AttachItem(ABaseItem* Item, EEquipmentAttachmentTarget Target);
	bool CompleteItemAttachment(ABaseItem* Item, EEquipmentAttachmentTarget Target, bool bAttached);
	void ClearBowArrowAnchor(ABowItem* ExpectedBow = nullptr);
	bool StoreCurrentEquippedItem(bool bRemoveStats = true);
	bool CanChangeEquipment() const;
	double LastEquipmentRequestTime = -1.0;
	void StartEquipItem(ABaseItem* Item, FGameplayTag SourceSlotTag);
	void FinalizePendingEquip();
	void CancelPendingEquip();
	void PlayEquipmentMontage(ABaseItem* Item, UAnimMontage* Montage, float PlayRate);
	void OnEquipmentMontageEnded(UAnimMontage* Montage, bool bInterrupted);
};
