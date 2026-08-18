#include "Equipment/PlayerEquipmentComponent.h"

#include "Animation/AnimSequenceBase.h"

#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BaseItem.h"
#include "BasePlayer.h"
#include "Equipment/WeaponAnimationDataAsset.h"
#include "Inventory/InventoryComponent.h"
#include "ItemSubSystem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "GASStrengthEquipmentGameplayEffect.h"

UPlayerEquipmentComponent::UPlayerEquipmentComponent()
{
	SetIsReplicatedByDefault(true);
	StrengthEquipmentEffectClass = UGASStrengthEquipmentGameplayEffect::StaticClass();
}

void UPlayerEquipmentComponent::BeginPlay()
{
	Super::BeginPlay();
	PlayerOwner = Cast<ABasePlayer>(GetOwner());
}

void UPlayerEquipmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UPlayerEquipmentComponent, EquipmentState);
}

bool UPlayerEquipmentComponent::IsEquipmentTransitioning() const
{
	return EquipmentState == EEquipmentState::Equipping || EquipmentState == EEquipmentState::Unequipping;
}

void UPlayerEquipmentComponent::EquipItemFromSlot(FGameplayTag KeyTag)
{
	if (!PlayerOwner)
	{
		PlayerOwner = Cast<ABasePlayer>(GetOwner());
	}

	if (!PlayerOwner)
	{
		return;
	}

	if (!PlayerOwner->HasAuthority())
	{
		Server_EquipItemFromSlot(KeyTag);
		return;
	}

	if (IsEquipmentTransitioning())
	{
		return;
	}

	const int32 RequestedSlotIndex = PlayerOwner->ItemSlots.IndexOfByKey(KeyTag);
	if (PlayerOwner->ItemSlots.IsValidIndex(RequestedSlotIndex))
	{
		StartEquipItemFromSlot(RequestedSlotIndex);
		PlayerOwner->OnItemSlotsChanged.Broadcast();
	}
}

bool UPlayerEquipmentComponent::EquipInventoryWeapon(FGameplayTag ItemTag)
{
	if (!PlayerOwner)
	{
		PlayerOwner = Cast<ABasePlayer>(GetOwner());
	}

	UInventoryComponent* Inventory = PlayerOwner ? PlayerOwner->GetInventoryComponent() : nullptr;
	if (!PlayerOwner || !PlayerOwner->HasAuthority() || !Inventory ||
		Inventory->GetMaterialCount(ItemTag) <= 0 || IsEquipmentTransitioning())
	{
		return false;
	}

	if (IsValid(PlayerOwner->EquippedItem) &&
		PlayerOwner->EquippedItem->ItemTag == ItemTag &&
		!IsItemOwnedByItemSlot(PlayerOwner->EquippedItem))
	{
		return true;
	}

	StoreCurrentEquippedItem();

	UItemSubsystem* ItemSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UItemSubsystem>() : nullptr;
	if (!ItemSubsystem)
	{
		return false;
	}

	ABaseItem* SpawnedItem = ItemSubsystem->SpawnItem(
		ItemTag,
		PlayerOwner->GetActorTransform(),
		EItemState::InItemSlot,
		PlayerOwner);
	if (!IsValid(SpawnedItem))
	{
		return false;
	}

	StartEquipItem(SpawnedItem, FGameplayTag());
	PlayerOwner->OnQuickSlotsChanged.Broadcast();
	return true;
}

void UPlayerEquipmentComponent::UnequipCurrentItem()
{
	if (!PlayerOwner)
	{
		PlayerOwner = Cast<ABasePlayer>(GetOwner());
	}

	if (!PlayerOwner || !PlayerOwner->HasAuthority() || IsEquipmentTransitioning())
	{
		return;
	}

	StoreCurrentEquippedItem();
	EquipmentState = EEquipmentState::None;
	PlayerOwner->OnItemSlotsChanged.Broadcast();
	PlayerOwner->OnQuickSlotsChanged.Broadcast();
}

void UPlayerEquipmentComponent::UseEquippedItem(bool bDestroy)
{
	if (!PlayerOwner)
	{
		PlayerOwner = Cast<ABasePlayer>(GetOwner());
	}

	if (!PlayerOwner || !PlayerOwner->HasAuthority() || PlayerOwner->EquippedItem == nullptr || IsEquipmentTransitioning())
	{
		return;
	}

	int32 EquippedIndex = PlayerOwner->ItemSlots.IndexOfByKey(PlayerOwner->EquippedItem.Get());
	if (EquippedIndex != INDEX_NONE)
	{
		UE_LOG(LogTemp, Log, TEXT("UPlayerEquipmentComponent::UseEquippedItem : Item used! Slot index: %d"), EquippedIndex);

		FGameplayTag AssignedKeyTag = ResolveUseKeyTag(PlayerOwner->EquippedItem);

		PlayerOwner->RemoveItemFromSlot(PlayerOwner->ItemSlots[EquippedIndex].KeyTag);
		PlayerOwner->RemoveAbilityFromSlot(AssignedKeyTag);

		if (bDestroy)
		{
			PlayerOwner->EquippedItem->Destroy();
		}

		PlayerOwner->EquippedItem = nullptr;
		EquipmentState = EEquipmentState::None;
		PlayerOwner->OnItemSlotsChanged.Broadcast();
	}
}

void UPlayerEquipmentComponent::OnRepOwnerEquippedItem()
{
	if (!PlayerOwner)
	{
		PlayerOwner = Cast<ABasePlayer>(GetOwner());
	}

	if (PlayerOwner && IsValid(PlayerOwner->EquippedItem) && PlayerOwner->EquippedItem->MyDefinition)
	{
		AttachItem(PlayerOwner->EquippedItem, EEquipmentAttachmentTarget::Equipped);
	}
}

FGameplayTag UPlayerEquipmentComponent::GetEquippedItemTag() const
{
	const ABasePlayer* OwnerPlayer = PlayerOwner ? PlayerOwner.Get() : Cast<ABasePlayer>(GetOwner());
	return OwnerPlayer && IsValid(OwnerPlayer->EquippedItem) ? OwnerPlayer->EquippedItem->ItemTag : FGameplayTag();
}

bool UPlayerEquipmentComponent::IsEquippedItemTag(FGameplayTag ItemTag) const
{
	const FGameplayTag EquippedItemTag = GetEquippedItemTag();
	return ItemTag.IsValid() && EquippedItemTag.IsValid() && EquippedItemTag.MatchesTag(ItemTag);
}

FGameplayTag UPlayerEquipmentComponent::GetEquippedUpperBodyOverlayTag() const
{
	const ABasePlayer* OwnerPlayer = PlayerOwner ? PlayerOwner.Get() : Cast<ABasePlayer>(GetOwner());
	const ABaseItem* EquippedItem = OwnerPlayer ? OwnerPlayer->EquippedItem : nullptr;
	const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(EquippedItem);
	if (!Entry || !Entry->bUseUpperBodyOverlay)
	{
		return FGameplayTag();
	}

	return Entry->UpperBodyOverlayTag.IsValid() ? Entry->UpperBodyOverlayTag : GetEquippedItemTag();
}

bool UPlayerEquipmentComponent::ShouldUseEquippedUpperBodyOverlay() const
{
	return GetEquippedUpperBodyOverlayTag().IsValid();
}

int32 UPlayerEquipmentComponent::GetEquippedUpperBodyOverlayIndex() const
{
	const ABasePlayer* OwnerPlayer = PlayerOwner ? PlayerOwner.Get() : Cast<ABasePlayer>(GetOwner());
	const ABaseItem* EquippedItem = OwnerPlayer ? OwnerPlayer->EquippedItem : nullptr;
	const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(EquippedItem);
	return Entry && Entry->bUseUpperBodyOverlay ? Entry->UpperBodyOverlayIndex : 0;
}

UAnimMontage* UPlayerEquipmentComponent::GetEquippedCombatIntroMontage() const
{
	const ABasePlayer* OwnerPlayer = PlayerOwner ? PlayerOwner.Get() : Cast<ABasePlayer>(GetOwner());
	const ABaseItem* EquippedItem = OwnerPlayer ? OwnerPlayer->EquippedItem : nullptr;
	const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(EquippedItem);
	return Entry ? Entry->CombatIntroMontage.Get() : nullptr;
}

float UPlayerEquipmentComponent::GetEquippedCombatIntroPlayRate() const
{
	const ABasePlayer* OwnerPlayer = PlayerOwner ? PlayerOwner.Get() : Cast<ABasePlayer>(GetOwner());
	const ABaseItem* EquippedItem = OwnerPlayer ? OwnerPlayer->EquippedItem : nullptr;
	const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(EquippedItem);
	return Entry ? Entry->CombatIntroPlayRate : 1.f;
}

UAnimMontage* UPlayerEquipmentComponent::GetEquippedReloadMontage() const
{
	const ABasePlayer* OwnerPlayer = PlayerOwner ? PlayerOwner.Get() : Cast<ABasePlayer>(GetOwner());
	const ABaseItem* EquippedItem = OwnerPlayer ? OwnerPlayer->EquippedItem : nullptr;
	const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(EquippedItem);
	return Entry ? Entry->ReloadMontage.Get() : nullptr;
}

float UPlayerEquipmentComponent::GetEquippedReloadPlayRate() const
{
	const ABasePlayer* OwnerPlayer = PlayerOwner ? PlayerOwner.Get() : Cast<ABasePlayer>(GetOwner());
	const ABaseItem* EquippedItem = OwnerPlayer ? OwnerPlayer->EquippedItem : nullptr;
	const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(EquippedItem);
	return Entry ? Entry->ReloadPlayRate : 1.f;
}

UAnimMontage* UPlayerEquipmentComponent::GetEquippedBasicAttackMontage() const
{
	const FWeaponAnimationEntry* Entry = GetEquippedWeaponAnimationEntry();
	return Entry ? Entry->BasicAttackMontage.Get() : nullptr;
}

TArray<FName> UPlayerEquipmentComponent::GetEquippedBasicAttackComboSections() const
{
	const FWeaponAnimationEntry* Entry = GetEquippedWeaponAnimationEntry();
	return Entry ? Entry->BasicAttackComboSections : TArray<FName>();
}

float UPlayerEquipmentComponent::GetEquippedBasicAttackPlayRate() const
{
	const FWeaponAnimationEntry* Entry = GetEquippedWeaponAnimationEntry();
	return Entry ? FMath::Max(Entry->BasicAttackPlayRate, KINDA_SMALL_NUMBER) : 1.f;
}

UAnimSequenceBase* UPlayerEquipmentComponent::GetEquippedPreviewIdleAnimation() const
{
	const ABasePlayer* OwnerPlayer = PlayerOwner ? PlayerOwner.Get() : Cast<ABasePlayer>(GetOwner());
	return GetPreviewIdleAnimationForItem(OwnerPlayer ? OwnerPlayer->EquippedItem : nullptr);
}

UAnimSequenceBase* UPlayerEquipmentComponent::GetPreviewIdleAnimationForItem(const ABaseItem* Item) const
{
	const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(Item);
	return Entry ? Entry->PreviewIdleAnimation.LoadSynchronous() : nullptr;
}

float UPlayerEquipmentComponent::GetEquippedPreviewIdlePlayRate() const
{
	const ABasePlayer* OwnerPlayer = PlayerOwner ? PlayerOwner.Get() : Cast<ABasePlayer>(GetOwner());
	return GetPreviewIdlePlayRateForItem(OwnerPlayer ? OwnerPlayer->EquippedItem : nullptr);
}

float UPlayerEquipmentComponent::GetPreviewIdlePlayRateForItem(const ABaseItem* Item) const
{
	const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(Item);
	return Entry ? FMath::Max(Entry->PreviewIdlePlayRate, KINDA_SMALL_NUMBER) : 1.f;
}

UAnimMontage* UPlayerEquipmentComponent::GetEquippedAimCycleMontage() const
{
	const FWeaponAnimationEntry* Entry = GetEquippedWeaponAnimationEntry();
	return Entry ? Entry->AimCycleMontage.Get() : nullptr;
}

TSubclassOf<UAnimInstance> UPlayerEquipmentComponent::GetEquippedWeaponAnimLayerClass() const
{
	const FWeaponAnimationEntry* Entry = GetEquippedWeaponAnimationEntry();
	return Entry ? Entry->WeaponAnimLayerClass : nullptr;
}

const FWeaponAnimationEntry* UPlayerEquipmentComponent::GetEquippedWeaponAnimationEntry() const
{
	const ABasePlayer* OwnerPlayer = PlayerOwner ? PlayerOwner.Get() : Cast<ABasePlayer>(GetOwner());
	return ResolveWeaponAnimationEntry(OwnerPlayer ? OwnerPlayer->EquippedItem : nullptr);
}

void UPlayerEquipmentComponent::OnRep_EquipmentState()
{
}

void UPlayerEquipmentComponent::Server_EquipItemFromSlot_Implementation(FGameplayTag KeyTag)
{
	EquipItemFromSlot(KeyTag);
}

const FWeaponAnimationEntry* UPlayerEquipmentComponent::ResolveWeaponAnimationEntry(const ABaseItem* Item) const
{
	if (const UWeaponAnimationDataAsset* AnimationData = ResolveWeaponAnimationData(Item))
	{
		return AnimationData->FindEntryForTag(Item->ItemTag);
	}

	return nullptr;
}

const UWeaponAnimationDataAsset* UPlayerEquipmentComponent::ResolveWeaponAnimationData(const ABaseItem* Item) const
{
	if (!Item)
	{
		return nullptr;
	}

	for (const FWeaponAnimationDataMapping& Mapping : WeaponAnimationDataByTag)
	{
		if (Mapping.AnimationData && Mapping.WeaponTag.IsValid() && Item->ItemTag.MatchesTag(Mapping.WeaponTag))
		{
			return Mapping.AnimationData.Get();
		}
	}

	return WeaponAnimationData.Get();
}

FResolvedEquipmentAttachment UPlayerEquipmentComponent::GetEquippedAttachmentProfile() const
{
	const ABasePlayer* OwnerPlayer = PlayerOwner ? PlayerOwner.Get() : Cast<ABasePlayer>(GetOwner());
	return GetEquippedAttachmentProfileForItem(OwnerPlayer ? OwnerPlayer->EquippedItem : nullptr);
}

FResolvedEquipmentAttachment UPlayerEquipmentComponent::GetEquippedAttachmentProfileForItem(const ABaseItem* Item) const
{
	return ResolveAttachmentProfile(Item, EEquipmentAttachmentTarget::Equipped);
}

FResolvedEquipmentAttachment UPlayerEquipmentComponent::GetPreviewAttachmentProfileForItem(const ABaseItem* Item) const
{
	FResolvedEquipmentAttachment Profile;
	const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(Item);
	Profile.ItemGripSocketName = Entry ? Entry->ItemGripSocketName : NAME_None;

	const FName ConfiguredSocketName = Entry ? Entry->EquipSocketName : NAME_None;
	const FName CandidateSocketNames[] =
	{
		ConfiguredSocketName,
		FName(TEXT("GripPoint")),
		FName(TEXT("hand_r"))
	};
	for (const FName SocketName : CandidateSocketNames)
	{
		if (IsCharacterSocketValid(SocketName))
		{
			Profile.CharacterSocketName = SocketName;
			break;
		}
	}

	return Profile;
}

FResolvedEquipmentAttachment UPlayerEquipmentComponent::ResolveAttachmentProfile(
	const ABaseItem* Item,
	EEquipmentAttachmentTarget Target) const

{
	FResolvedEquipmentAttachment Profile;
	Profile.CharacterSocketName = ResolveCharacterSocketName(Item, Target);

	if (const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(Item))
	{
		Profile.ItemGripSocketName = Entry->ItemGripSocketName;
	}

	return Profile;
}

FName UPlayerEquipmentComponent::ResolveCharacterSocketName(
	const ABaseItem* Item,
	EEquipmentAttachmentTarget Target) const
{
	FName ConfiguredSocketName = NAME_None;
	if (const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(Item))
	{
		ConfiguredSocketName = Target == EEquipmentAttachmentTarget::Equipped
			? Entry->EquipSocketName
			: Entry->StoredSocketName;

		if (IsCharacterSocketValid(ConfiguredSocketName))
		{
			return ConfiguredSocketName;
		}

		if (!ConfiguredSocketName.IsNone())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("UPlayerEquipmentComponent::ResolveCharacterSocketName : Configured socket %s does not exist for item %s."),
				*ConfiguredSocketName.ToString(),
				*GetNameSafe(Item));
		}
	}

	// Item feature data remains the compatibility fallback for the equipped
	// hand socket. Stored items use the explicit back socket fallback below.
	if (Target == EEquipmentAttachmentTarget::Equipped)
	{
		if (UItemSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UItemSubsystem>() : nullptr)
		{
			const FName ItemFeatureSocket = Subsystem->GetAttachmentSocketName(Item ? Item->ItemTag : FGameplayTag());
			if (IsCharacterSocketValid(ItemFeatureSocket))
			{
				return ItemFeatureSocket;
			}
		}
	}

	const FName FallbackSocket = Target == EEquipmentAttachmentTarget::Equipped
		? FName(TEXT("GripPoint"))
		: FName(TEXT("BackWeaponSocket"));
	if (IsCharacterSocketValid(FallbackSocket))
	{
		return FallbackSocket;
	}

	UE_LOG(LogTemp, Error,
		TEXT("UPlayerEquipmentComponent::ResolveCharacterSocketName : No valid %s socket found for item %s. Configured=%s Fallback=%s"),
		Target == EEquipmentAttachmentTarget::Equipped ? TEXT("equipped") : TEXT("stored"),
		*GetNameSafe(Item),
		*ConfiguredSocketName.ToString(),
		*FallbackSocket.ToString());
	return NAME_None;
}

bool UPlayerEquipmentComponent::IsCharacterSocketValid(FName SocketName) const
{
	const ABasePlayer* OwnerPlayer = PlayerOwner ? PlayerOwner.Get() : Cast<ABasePlayer>(GetOwner());
	return OwnerPlayer && OwnerPlayer->GetMesh() && !SocketName.IsNone() && OwnerPlayer->GetMesh()->DoesSocketExist(SocketName);
}

FGameplayTag UPlayerEquipmentComponent::ResolveUseKeyTag(const ABaseItem* Item) const
{
	FGameplayTag UseKeyTag = Key_Default_Mouse_LeftClick;
	if (UWorld* World = GetWorld())
	{
		if (UItemSubsystem* Subsystem = World->GetSubsystem<UItemSubsystem>())
		{
			const FGameplayTag SubsystemUseKeyTag = Subsystem->GetUseKeyTag(Item ? Item->ItemTag : FGameplayTag());
			if (SubsystemUseKeyTag.IsValid())
			{
				UseKeyTag = SubsystemUseKeyTag;
			}
		}
	}

	return UseKeyTag;
}

bool UPlayerEquipmentComponent::CanUseEquippedItemAbility(const ABaseItem* Item) const
{
	if (!Item || !PlayerOwner)
	{
		return false;
	}

	const TArray<FGameplayTag>& RequiredTags = Item->GetCanUseAbilityList();
	if (RequiredTags.IsEmpty())
	{
		return true;
	}

	UAbilitySystemComponent* ASC = PlayerOwner->GetAbilitySystemComponent();
	if (!ASC)
	{
		return false;
	}

	for (const FGameplayTag& Tag : RequiredTags)
	{
		if (ASC->HasMatchingGameplayTag(Tag))
		{
			return true;
		}
	}

	return false;
}

void UPlayerEquipmentComponent::CancelActiveWeaponAbilities() const
{
	if (!PlayerOwner)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = PlayerOwner->GetAbilitySystemComponent())
	{
		FGameplayTagContainer WeaponActionTags;
		WeaponActionTags.AddTag(GameplayAbility_Weapon_AimCycle);
		WeaponActionTags.AddTag(GameplayAbility_BasicAttack);
		ASC->CancelAbilities(&WeaponActionTags, nullptr, nullptr);
	}
}

void UPlayerEquipmentComponent::GrantEquippedItemAbility(ABaseItem* Item)
{
	if (!PlayerOwner || !Item || !CanUseEquippedItemAbility(Item))
	{
		return;
	}

	if (const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(Item))
	{
		for (const TPair<FGameplayTag, TSubclassOf<UGameplayAbility>>& AbilityPair : Entry->GrantedAbilitiesByInputTag)
		{
			if (AbilityPair.Key.IsValid() && AbilityPair.Value)
			{
				PlayerOwner->GrantAbilityToSlot(AbilityPair.Key, AbilityPair.Value);
				UE_LOG(LogTemp, Log, TEXT("UPlayerEquipmentComponent::GrantEquippedItemAbility : Granted weapon DA ability %s for item %s to key %s"), *AbilityPair.Value->GetName(), *Item->GetName(), *AbilityPair.Key.ToString());
			}
		}
	}

	if (const TSubclassOf<UGameplayAbility> GrantedAbilityClass = Item->GetGrantedAbilityClass())
	{
		const FGameplayTag AssignKeyTag = ResolveUseKeyTag(Item);
		PlayerOwner->GrantAbilityToSlot(AssignKeyTag, GrantedAbilityClass);
		UE_LOG(LogTemp, Log, TEXT("UPlayerEquipmentComponent::GrantEquippedItemAbility : Granted ability %s for item %s to key %s"), *GrantedAbilityClass->GetName(), *Item->GetName(), *AssignKeyTag.ToString());
	}
}

void UPlayerEquipmentComponent::RemoveEquippedItemAbility(ABaseItem* Item)
{
	if (PlayerOwner && Item)
	{
		if (const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(Item))
		{
			for (const TPair<FGameplayTag, TSubclassOf<UGameplayAbility>>& AbilityPair : Entry->GrantedAbilitiesByInputTag)
			{
				if (AbilityPair.Key.IsValid())
				{
					PlayerOwner->RemoveAbilityFromSlot(AbilityPair.Key);
				}
			}
		}

		PlayerOwner->RemoveAbilityFromSlot(ResolveUseKeyTag(Item));
	}
}

bool UPlayerEquipmentComponent::AttachItem(ABaseItem* Item, EEquipmentAttachmentTarget Target) const
{
	if (!PlayerOwner || !Item || !PlayerOwner->GetMesh())
	{
		return false;
	}

	const FResolvedEquipmentAttachment Profile = ResolveAttachmentProfile(Item, Target);
	if (!Profile.IsValid())
	{
		return false;
	}

	const FName CharacterSocketName = Profile.CharacterSocketName;

	if (UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(Item->GetRootComponent()))
	{
		MeshComp->SetSimulatePhysics(false);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	const FName ItemGripSocketName = Profile.ItemGripSocketName;
	if (ItemGripSocketName.IsNone())
	{
		const bool bAttached = Item->AttachToComponent(
			PlayerOwner->GetMesh(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			CharacterSocketName);
		return bAttached;
	}

	USceneComponent* GripComponent = Item->GetAttachmentReferenceComponent();

	if (!GripComponent || !GripComponent->DoesSocketExist(ItemGripSocketName) || !Item->GetRootComponent())
	{
		UE_LOG(LogTemp, Warning, TEXT("UPlayerEquipmentComponent::AttachItem : Item %s has no grip socket %s on its attachment reference component. Falling back to root attachment."), *GetNameSafe(Item), *ItemGripSocketName.ToString());
		return Item->AttachToComponent(
			PlayerOwner->GetMesh(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			CharacterSocketName);
	}

	const FTransform RootWorldTransform = Item->GetRootComponent()->GetComponentTransform();
	const FTransform GripWorldTransform = GripComponent->GetSocketTransform(ItemGripSocketName, RTS_World);
	const FTransform GripRelativeToRoot = GripWorldTransform.GetRelativeTransform(RootWorldTransform);
	const FTransform TargetSocketWorldTransform = PlayerOwner->GetMesh()->GetSocketTransform(CharacterSocketName, RTS_World);

	Item->SetActorTransform(GripRelativeToRoot.Inverse() * TargetSocketWorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
	const bool bAttached = Item->AttachToComponent(
		PlayerOwner->GetMesh(),
		FAttachmentTransformRules::KeepWorldTransform,
		CharacterSocketName);
	if (!bAttached)
	{
		return false;
	}

	const float AlignmentError = FVector::Distance(
		GripComponent->GetSocketLocation(ItemGripSocketName),
		PlayerOwner->GetMesh()->GetSocketLocation(CharacterSocketName));
	UE_LOG(LogTemp, Log, TEXT("UPlayerEquipmentComponent::AttachItem : Item=%s Target=%s CharacterSocket=%s GripSocket=%s AlignmentError=%.4f"),
		*GetNameSafe(Item),
		Target == EEquipmentAttachmentTarget::Equipped ? TEXT("Equipped") : TEXT("Stored"),
		*CharacterSocketName.ToString(),
		*ItemGripSocketName.ToString(),
		AlignmentError);
	return true;
}

bool UPlayerEquipmentComponent::IsItemOwnedByItemSlot(const ABaseItem* Item) const
{
	return PlayerOwner && IsValid(Item) && PlayerOwner->ItemSlots.ContainsByPredicate([Item](const FItemSlot& Slot)
	{
		return Slot.Item == Item;
	});
}

void UPlayerEquipmentComponent::StoreCurrentEquippedItem()
{
	CancelActiveWeaponAbilities();

	if (!PlayerOwner || !IsValid(PlayerOwner->EquippedItem))
	{
		return;
	}

	ABaseItem* PreviousItem = PlayerOwner->EquippedItem;
	const bool bOwnedByItemSlot = IsItemOwnedByItemSlot(PreviousItem);
	PreviousItem->RemoveStrengthBonusEffect();
	RemoveEquippedItemAbility(PreviousItem);
	PlayerOwner->EquippedItem = nullptr;

	if (bOwnedByItemSlot)
	{
		PreviousItem->SetItemState(EItemState::InItemSlot);
	}
	else
	{
		// Inventory-backed weapons are transient equipped representations. The
		// inventory keeps the item count, so the actor must not survive unequip.
		PreviousItem->Destroy();
	}

	PlayerOwner->SetCombatMode(false);
}

void UPlayerEquipmentComponent::StartEquipItemFromSlot(int32 SlotIndex)
{
	if (!PlayerOwner || !PlayerOwner->HasAuthority() || !PlayerOwner->ItemSlots.IsValidIndex(SlotIndex))
	{
		return;
	}

	ABaseItem* SlotItem = PlayerOwner->ItemSlots[SlotIndex].Item;

	if (PlayerOwner->EquippedItem == SlotItem)
	{
		StoreCurrentEquippedItem();
		EquipmentState = EEquipmentState::None;
		return;
	}

	StoreCurrentEquippedItem();

	if (!IsValid(SlotItem))
	{
		EquipmentState = EEquipmentState::None;
		return;
	}

	StartEquipItem(SlotItem, PlayerOwner->ItemSlots[SlotIndex].KeyTag);
}

void UPlayerEquipmentComponent::StartEquipItem(ABaseItem* Item, FGameplayTag SourceSlotTag)
{
	if (!PlayerOwner || !PlayerOwner->HasAuthority() || !IsValid(Item))
	{
		return;
	}

	PendingEquipItem = Item;
	PendingEquipSlotTag = SourceSlotTag;
	EquipmentState = EEquipmentState::Equipping;

	const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(Item);
	UAnimMontage* EquipMontage = Entry ? Entry->EquipMontage.Get() : nullptr;
	const float PlayRate = Entry ? Entry->EquipPlayRate : 1.f;

	if (EquipMontage)
	{
		Multicast_PlayEquipmentMontage(Item, EquipMontage, PlayRate);
	}
	else
	{
		FinalizePendingEquip();
	}
}

void UPlayerEquipmentComponent::FinalizePendingEquip()
{
	if (!PlayerOwner || !PlayerOwner->HasAuthority())
	{
		return;
	}

	ABaseItem* ItemToEquip = PendingEquipItem.Get();
	if (!IsValid(ItemToEquip))
	{
		CancelPendingEquip();
		return;
	}

	if (PlayerOwner->EquippedItem != ItemToEquip)
	{
		ItemToEquip->SetItemState(EItemState::Equipped);
		if (!AttachItem(ItemToEquip, EEquipmentAttachmentTarget::Equipped))
		{
			UE_LOG(LogTemp, Error,
				TEXT("UPlayerEquipmentComponent::FinalizePendingEquip : Failed to attach item %s. Equip cancelled."),
				*GetNameSafe(ItemToEquip));
			CancelPendingEquip();
			return;
		}

		PlayerOwner->EquippedItem = ItemToEquip;
		if (!ItemToEquip->ApplyStrengthBonusEffect(PlayerOwner->GetAbilitySystemComponent(), StrengthEquipmentEffectClass))
		{
			UE_LOG(LogTemp, Warning, TEXT("UPlayerEquipmentComponent::FinalizePendingEquip: failed to apply Strength GE for %s."), *GetNameSafe(ItemToEquip));
		}
		GrantEquippedItemAbility(ItemToEquip);
		PlayerOwner->EnterCombatModeFromEquipment();
	}

	EquipmentState = EEquipmentState::Equipped;
	PendingEquipItem = nullptr;
	PendingEquipSlotTag = FGameplayTag();
	ActiveEquipmentMontage = nullptr;
	PlayerOwner->OnItemSlotsChanged.Broadcast();
}

void UPlayerEquipmentComponent::CancelPendingEquip()
{
	if (PlayerOwner && PlayerOwner->HasAuthority())
	{
		if (ABaseItem* ItemToCancel = PendingEquipItem.Get())
		{
			if (IsItemOwnedByItemSlot(ItemToCancel))
			{
				ItemToCancel->SetItemState(EItemState::InItemSlot);
			}
			else if (ItemToCancel != PlayerOwner->EquippedItem)
			{
				ItemToCancel->Destroy();
			}
		}

		EquipmentState = IsValid(PlayerOwner->EquippedItem) ? EEquipmentState::Equipped : EEquipmentState::None;
	}

	PendingEquipItem = nullptr;
	PendingEquipSlotTag = FGameplayTag();
	ActiveEquipmentMontage = nullptr;
}

void UPlayerEquipmentComponent::PlayEquipmentMontage(ABaseItem* Item, UAnimMontage* Montage, float PlayRate)
{
	if (!PlayerOwner || !Montage || !PlayerOwner->GetMesh())
	{
		return;
	}

	UAnimInstance* AnimInstance = PlayerOwner->GetMesh()->GetAnimInstance();
	if (!AnimInstance)
	{
		return;
	}

	PendingEquipItem = Item;
	ActiveEquipmentMontage = Montage;
	AnimInstance->Montage_Play(Montage, PlayRate > 0.f ? PlayRate : 1.f);

	FOnMontageEnded MontageEndedDelegate;
	MontageEndedDelegate.BindUObject(this, &UPlayerEquipmentComponent::OnEquipmentMontageEnded);
	AnimInstance->Montage_SetEndDelegate(MontageEndedDelegate, Montage);
}

void UPlayerEquipmentComponent::OnEquipmentMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != ActiveEquipmentMontage)
	{
		return;
	}

	if (bInterrupted)
	{
		CancelPendingEquip();
		return;
	}

	if (PlayerOwner && PlayerOwner->HasAuthority() && EquipmentState == EEquipmentState::Equipping && IsValid(PendingEquipItem))
	{
		FinalizePendingEquip();
		return;
	}

	ActiveEquipmentMontage = nullptr;
	PendingEquipItem = nullptr;
	EquipmentState = PlayerOwner && IsValid(PlayerOwner->EquippedItem) ? EEquipmentState::Equipped : EEquipmentState::None;
}

void UPlayerEquipmentComponent::HandleEquipmentAttachNotify()
{
	if (!IsValid(PendingEquipItem))
	{
		return;
	}

	if (!AttachItem(PendingEquipItem, EEquipmentAttachmentTarget::Equipped))
	{
		if (PlayerOwner && PlayerOwner->HasAuthority())
		{
			CancelPendingEquip();
		}
		return;
	}

	if (PlayerOwner && PlayerOwner->HasAuthority())
	{
		FinalizePendingEquip();
	}
}

void UPlayerEquipmentComponent::Multicast_PlayEquipmentMontage_Implementation(ABaseItem* Item, UAnimMontage* Montage, float PlayRate)
{
	EquipmentState = EEquipmentState::Equipping;
	PlayEquipmentMontage(Item, Montage, PlayRate);
}
