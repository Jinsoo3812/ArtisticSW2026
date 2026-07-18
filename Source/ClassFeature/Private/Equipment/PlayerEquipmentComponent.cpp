#include "Equipment/PlayerEquipmentComponent.h"

#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BaseItem.h"
#include "BasePlayer.h"
#include "Equipment/WeaponAnimationDataAsset.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "ItemSubSystem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

UPlayerEquipmentComponent::UPlayerEquipmentComponent()
{
	SetIsReplicatedByDefault(true);
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
		AttachItemToSocket(PlayerOwner->EquippedItem, ResolveEquipSocketName(PlayerOwner->EquippedItem));
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

FName UPlayerEquipmentComponent::ResolveEquipSocketName(const ABaseItem* Item) const
{
	if (const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(Item))
	{
		if (!Entry->EquipSocketName.IsNone())
		{
			return Entry->EquipSocketName;
		}
	}

	FName SocketName = TEXT("GripPoint");
	if (UWorld* World = GetWorld())
	{
		if (UItemSubsystem* Subsystem = World->GetSubsystem<UItemSubsystem>())
		{
			const FGameplayTag ItemTag = Item ? Item->ItemTag : FGameplayTag();
			const FName SubsystemSocketName = Subsystem->GetAttachmentSocketName(ItemTag);
			if (!SubsystemSocketName.IsNone())
			{
				SocketName = SubsystemSocketName;
			}
		}
	}

	return SocketName;
}

FName UPlayerEquipmentComponent::ResolveItemGripSocketName(const ABaseItem* Item) const
{
	if (const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(Item))
	{
		return Entry->ItemGripSocketName;
	}

	return NAME_None;
}

FName UPlayerEquipmentComponent::ResolveStoredSocketName(const ABaseItem* Item) const
{
	if (const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(Item))
	{
		if (!Entry->StoredSocketName.IsNone())
		{
			return Entry->StoredSocketName;
		}
	}

	return TEXT("BackWeaponSocket");
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

void UPlayerEquipmentComponent::AttachItemToSocket(ABaseItem* Item, FName SocketName) const
{
	if (!PlayerOwner || !Item || !PlayerOwner->GetMesh())
	{
		return;
	}

	if (UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(Item->GetRootComponent()))
	{
		MeshComp->SetSimulatePhysics(false);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	const FName ItemGripSocketName = ResolveItemGripSocketName(Item);
	if (ItemGripSocketName.IsNone())
	{
		Item->AttachToComponent(PlayerOwner->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
		return;
	}

	USceneComponent* GripComponent = Item->GetAttachmentReferenceComponent();

	if (!GripComponent || !GripComponent->DoesSocketExist(ItemGripSocketName) || !Item->GetRootComponent())
	{
		UE_LOG(LogTemp, Warning, TEXT("UPlayerEquipmentComponent::AttachItemToSocket : Item %s has no grip socket %s on its attachment reference component. Falling back to root attachment."), *GetNameSafe(Item), *ItemGripSocketName.ToString());
		Item->AttachToComponent(PlayerOwner->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
		return;
	}

	const FTransform RootWorldTransform = Item->GetRootComponent()->GetComponentTransform();
	const FTransform GripWorldTransform = GripComponent->GetSocketTransform(ItemGripSocketName, RTS_World);
	const FTransform GripRelativeToRoot = GripWorldTransform.GetRelativeTransform(RootWorldTransform);
	const FTransform TargetSocketWorldTransform = PlayerOwner->GetMesh()->GetSocketTransform(SocketName, RTS_World);

	Item->SetActorTransform(GripRelativeToRoot.Inverse() * TargetSocketWorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
	Item->AttachToComponent(PlayerOwner->GetMesh(), FAttachmentTransformRules::KeepWorldTransform, SocketName);

	const float AlignmentError = FVector::Distance(
		GripComponent->GetSocketLocation(ItemGripSocketName),
		PlayerOwner->GetMesh()->GetSocketLocation(SocketName));
	UE_LOG(LogTemp, Log, TEXT("UPlayerEquipmentComponent::AttachItemToSocket : Item=%s EquipSocket=%s GripSocket=%s AlignmentError=%.4f"),
		*GetNameSafe(Item), *SocketName.ToString(), *ItemGripSocketName.ToString(), AlignmentError);
}

void UPlayerEquipmentComponent::StoreCurrentEquippedItem()
{
	if (!PlayerOwner || !IsValid(PlayerOwner->EquippedItem))
	{
		return;
	}

	RemoveEquippedItemAbility(PlayerOwner->EquippedItem);
	PlayerOwner->EquippedItem->SetItemState(EItemState::InItemSlot);
	PlayerOwner->EquippedItem = nullptr;
	PlayerOwner->SetCombatMode(false);
}

void UPlayerEquipmentComponent::StartEquipItemFromSlot(int32 SlotIndex)
{
	if (!PlayerOwner || !PlayerOwner->HasAuthority() || !PlayerOwner->ItemSlots.IsValidIndex(SlotIndex))
	{
		return;
	}

	ABaseItem* SlotItem = PlayerOwner->ItemSlots[SlotIndex].Item;
	if (UAbilitySystemComponent* ASC = PlayerOwner->GetAbilitySystemComponent())
	{
		FGameplayTagContainer AimCycleAbilityTags;
		AimCycleAbilityTags.AddTag(GameplayAbility_Weapon_AimCycle);
		ASC->CancelAbilities(&AimCycleAbilityTags, nullptr, nullptr);
	}

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

	PendingEquipItem = SlotItem;
	PendingEquipSlotTag = PlayerOwner->ItemSlots[SlotIndex].KeyTag;
	EquipmentState = EEquipmentState::Equipping;

	const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(SlotItem);
	UAnimMontage* EquipMontage = Entry ? Entry->EquipMontage.Get() : nullptr;
	const float PlayRate = Entry ? Entry->EquipPlayRate : 1.f;

	if (EquipMontage)
	{
		Multicast_PlayEquipmentMontage(SlotItem, EquipMontage, PlayRate);
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
		PlayerOwner->EquippedItem = ItemToEquip;
		PlayerOwner->EquippedItem->SetItemState(EItemState::Equipped);
		AttachItemToSocket(PlayerOwner->EquippedItem, ResolveEquipSocketName(PlayerOwner->EquippedItem));
		GrantEquippedItemAbility(PlayerOwner->EquippedItem);
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

	AttachItemToSocket(PendingEquipItem, ResolveEquipSocketName(PendingEquipItem));

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
