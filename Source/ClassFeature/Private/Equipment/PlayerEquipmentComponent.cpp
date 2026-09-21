#include "Equipment/PlayerEquipmentComponent.h"

#include "Animation/AnimSequenceBase.h"

#include "AbilitySystemComponent.h"
#include "Equipment/WeaponDefinition.h"
#include "GAS/Ability/WeaponGameplayAbility.h"
#include "BaseGameplayTags.h"
#include "BaseItem.h"
#include "ItemData.h"
#include "BasePlayer.h"
#include "Equipment/WeaponAnimationDataAsset.h"
#include "Inventory/InventoryComponent.h"
#include "ItemSubSystem.h"
#include "Item/Weapons/BowItem.h"
#include "Item/Weapons/SwordItem.h"
#include "Item/Projectiles/ArrowProjectile.h"
#include "BaseAttributeSet.h"
#include "SwimmingComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "GASStrengthEquipmentGameplayEffect.h"
#include "Components/EquipmentStatComponent.h"

UPlayerEquipmentComponent::UPlayerEquipmentComponent()
{
	SetIsReplicatedByDefault(true);
	StrengthEquipmentEffectClass = UGASStrengthEquipmentGameplayEffect::StaticClass();
}

void UPlayerEquipmentComponent::BeginPlay()
{
	Super::BeginPlay();
	PlayerOwner = Cast<ABasePlayer>(GetOwner());
	if (PlayerOwner && PlayerOwner->HasAuthority())
	{
		PlayerOwner->OnAbilitySystemInitialized.AddUObject(this, &ThisClass::BindOwnerAbilitySystem);
		BindOwnerAbilitySystem();
	}
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

bool UPlayerEquipmentComponent::EquipInventoryItem(FGameplayTag ItemTag)
{
	if (!PlayerOwner)
	{
		PlayerOwner = Cast<ABasePlayer>(GetOwner());
	}

	UInventoryComponent* Inventory = PlayerOwner ? PlayerOwner->GetInventoryComponent() : nullptr;
	if (!PlayerOwner || !PlayerOwner->HasAuthority() || !Inventory ||
		Inventory->GetMaterialCount(ItemTag) <= 0 || IsEquipmentTransitioning() || !CanChangeEquipment())
	{
		return false;
	}

	if (IsValid(PlayerOwner->EquippedItem) &&
		PlayerOwner->EquippedItem->ItemTag == ItemTag)
	{
		return true;
	}

	UItemSubsystem* ItemSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UItemSubsystem>() : nullptr;
	if (!ItemSubsystem)
	{
		return false;
	}

	// Inventory quick slots now own the request path formerly handled by the slot RPC.
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastEquipmentRequestTime < 0.15) return false;
	LastEquipmentRequestTime = Now;

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

	if (!PlayerOwner || !PlayerOwner->HasAuthority() || IsEquipmentTransitioning() || !CanChangeEquipment())
	{
		return;
	}

	if (!StoreCurrentEquippedItem()) return;
	EquipmentState = EEquipmentState::None;
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

	const FGameplayTag ItemTag = PlayerOwner->EquippedItem->ItemTag;
	// Finish stat cleanup before consuming inventory; failure must leave the count intact.
	if (!StoreCurrentEquippedItem()) return;
	if (bDestroy && PlayerOwner->GetInventoryComponent())
	{
		PlayerOwner->GetInventoryComponent()->RemoveItem(ItemTag, 1);
	}
	EquipmentState = EEquipmentState::None;
	PlayerOwner->OnQuickSlotsChanged.Broadcast();
}

void UPlayerEquipmentComponent::OnRepOwnerEquippedItem()
{
	if (PendingPresentationItem.IsValid()) PendingPresentationItem->OnItemInitialized.RemoveAll(this);
	PendingPresentationItem.Reset();
	if (!PlayerOwner)
	{
		PlayerOwner = Cast<ABasePlayer>(GetOwner());
	}

	if (PlayerOwner && IsValid(PlayerOwner->EquippedItem) && !PlayerOwner->EquippedItem->IsItemInitialized())
	{
		PendingPresentationItem = PlayerOwner->EquippedItem;
		PendingPresentationItem->OnItemInitialized.AddUObject(this, &ThisClass::OnEquippedItemInitialized);
	}
	if (PlayerOwner && IsValid(PlayerOwner->EquippedItem) && PlayerOwner->EquippedItem->MyDefinition)
	{
		if (AttachItem(PlayerOwner->EquippedItem, EEquipmentAttachmentTarget::Equipped))
		{
			return;
		}
	}

	ClearBowArrowAnchor();
}

FGameplayTag UPlayerEquipmentComponent::GetEquippedItemTag() const
{
	const ABasePlayer* OwnerPlayer = PlayerOwner ? PlayerOwner.Get() : Cast<ABasePlayer>(GetOwner());
	return OwnerPlayer && IsValid(OwnerPlayer->EquippedItem) ? OwnerPlayer->EquippedItem->ItemTag : FGameplayTag();
}

void UPlayerEquipmentComponent::OnEquippedItemInitialized(ABaseItem* Item)
{
	if (PlayerOwner && PlayerOwner->EquippedItem == Item) OnRepOwnerEquippedItem();
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
	if (!Entry || !Entry->bUseUpperBodyOverlay)
	{
		return 0;
	}

	if (Entry->UpperBodyOverlayIndex > 0)
	{
		return Entry->UpperBodyOverlayIndex;
	}

	// Fallback heuristic: If UpperBodyOverlayIndex was not explicitly configured (> 0),
	// default to standard ABP indices (1: Bow, 2: Sword in the player animation graph).
	const FGameplayTag EquippedTag = EquippedItem ? EquippedItem->ItemTag : FGameplayTag();
	const FString TagStr = EquippedTag.ToString();
	if (TagStr.Contains(TEXT("Bow")))
	{
		return 1;
	}
	if (TagStr.Contains(TEXT("Sword")) || TagStr.Contains(TEXT("OneHanded")) || TagStr.Contains(TEXT("Melee")))
	{
		return 2;
	}

	return 0;
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

	const UEquippableWeaponDefinition* Definition = Item->GetWeaponDefinition();
	return Definition ? Definition->AnimationData.Get() : nullptr;
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
	if (!PlayerOwner) return;
	const FEquipmentGrant* Grant = ItemGrants.Find(PlayerOwner->EquippedItem);
	if (Grant && Grant->ASC.IsValid())
		for (const FGameplayAbilitySpecHandle Handle : Grant->Abilities)
			Grant->ASC->CancelAbilityHandle(Handle);
}

bool UPlayerEquipmentComponent::ValidateWeapon(ABaseItem* Item) const
{
	if (!Item || !PlayerOwner) return false;
	const UEquippableWeaponDefinition* Definition = Item->GetWeaponDefinition();
	if (!Definition)
		return Item->MyDefinition && Item->MyDefinition->ProgressionKind != EItemProgressionKind::Weapon;
	UAbilitySystemComponent* ASC = PlayerOwner->GetAbilitySystemComponent();
	if (!ASC || !Definition->AbilitySet || Definition->AbilitySet->Abilities.IsEmpty()
		|| !Definition->AnimationData || !Definition->CombatData
		|| !FMath::IsFinite(Definition->CombatData->StrengthBonus) || Definition->CombatData->StrengthBonus < 0.f
		|| !FMath::IsFinite(Definition->CombatData->AttackCoefficient) || Definition->CombatData->AttackCoefficient < 0.f
		|| (!Definition->AllowedRoleTags.IsEmpty() && !ASC->HasAnyMatchingGameplayTags(Definition->AllowedRoleTags)))
		return false;
	const FWeaponAnimationEntry* Animation = Definition->AnimationData->FindEntryForTag(Item->ItemTag);
	if (Cast<ASwordItem>(Item) && (!Animation || !Animation->BasicAttackMontage)) return false;
	if (Cast<ABowItem>(Item))
	{
		UClass* Projectile = Definition->CombatData->ProjectileClass.LoadSynchronous();
		if (!Animation || !Animation->AimCycleMontage || !Projectile || !Projectile->IsChildOf(AArrowProjectile::StaticClass())) return false;
	}
	TSet<FGameplayTag> Inputs;
	for (const FWeaponAbilityEntry& Entry : Definition->AbilitySet->Abilities)
	{
		if (!Entry.InputTag.IsValid() || !Entry.AbilityClass || Entry.Level < 1
			|| Entry.AbilityClass->HasAnyClassFlags(CLASS_Abstract)
			|| !Entry.AbilityClass->IsChildOf(UWeaponGameplayAbility::StaticClass())
			|| Entry.AbilityClass->GetDefaultObject<UGameplayAbility>()->GetInstancingPolicy() != EGameplayAbilityInstancingPolicy::InstancedPerActor
			|| Inputs.Contains(Entry.InputTag)) return false;
		Inputs.Add(Entry.InputTag);
	}
	for (const TSubclassOf<UGameplayEffect>& Effect : Definition->EquipEffects)
	{
		if (!Effect || Effect->IsChildOf(UGASStrengthEquipmentGameplayEffect::StaticClass())
			|| Effect->GetDefaultObject<UGameplayEffect>()->DurationPolicy != EGameplayEffectDurationType::Infinite
			|| Effect->GetDefaultObject<UGameplayEffect>()->StackingType != EGameplayEffectStackingType::None)
			return false;
		for (const FGameplayModifierInfo& Modifier : Effect->GetDefaultObject<UGameplayEffect>()->Modifiers)
			if (Modifier.Attribute == UBaseAttributeSet::GetStrengthAttribute()) return false;
	}
	return true;
}

bool UPlayerEquipmentComponent::GrantEquippedItemAbility(ABaseItem* Item)
{
	if (!PlayerOwner || !PlayerOwner->HasAuthority() || !ValidateWeapon(Item)) return false;
	if (ItemGrants.Contains(Item)) return true;
	UAbilitySystemComponent* ASC = PlayerOwner->GetAbilitySystemComponent();
	if (!ASC) return false;
	ItemGrants.Add(Item).ASC = ASC;
	BindOwnerAbilitySystem();
	Item->OnDestroyed.AddUniqueDynamic(this, &ThisClass::OnGrantedItemDestroyed);
	Item->OnEndPlay.AddUniqueDynamic(this, &ThisClass::OnGrantedItemEndPlay);
	if (const UEquippableWeaponDefinition* Definition = Item->GetWeaponDefinition())
	{
		for (const FWeaponAbilityEntry& Entry : Definition->AbilitySet->Abilities)
		{
			FGameplayAbilitySpec Spec(Entry.AbilityClass, Entry.Level, INDEX_NONE, Item);
			Spec.GetDynamicSpecSourceTags().AddTag(Entry.InputTag);
			const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(Spec);
			if (!Handle.IsValid()) { RemoveEquippedItemAbility(Item); return false; }
			FEquipmentGrant* Grant = ItemGrants.Find(Item);
			if (!Grant) { ASC->CancelAbilityHandle(Handle); ASC->ClearAbility(Handle); return false; }
			Grant->Abilities.Add(Handle);
		}
		for (const TSubclassOf<UGameplayEffect>& Effect : Definition->EquipEffects)
		{
			FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
			Context.AddSourceObject(Item);
			FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(Effect, 1.f, Context);
			if (!Spec.IsValid()) { RemoveEquippedItemAbility(Item); return false; }
			const FActiveGameplayEffectHandle Handle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
			if (!Handle.IsValid()) { RemoveEquippedItemAbility(Item); return false; }
			FEquipmentGrant* Grant = ItemGrants.Find(Item);
			if (!Grant) { ASC->RemoveActiveGameplayEffect(Handle); return false; }
			Grant->Effects.Add(Handle);
		}
	}
	else if (CanUseEquippedItemAbility(Item))
	{
		if (const TSubclassOf<UGameplayAbility> Ability = Item->GetGrantedAbilityClass())
		{
			if (Ability->IsChildOf(UWeaponGameplayAbility::StaticClass()))
			{ RemoveEquippedItemAbility(Item); return false; }
			FGameplayAbilitySpec Spec(Ability, 1, INDEX_NONE, Item);
			Spec.GetDynamicSpecSourceTags().AddTag(ResolveUseKeyTag(Item));
			const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(Spec);
			FEquipmentGrant* Grant = ItemGrants.Find(Item);
			if (!Handle.IsValid() || !Grant)
			{
				ASC->CancelAbilityHandle(Handle);
				ASC->ClearAbility(Handle);
				RemoveEquippedItemAbility(Item);
				return false;
			}
			Grant->Abilities.Add(Handle);
		}
	}
	return true;
}

void UPlayerEquipmentComponent::RemoveEquippedItemAbility(ABaseItem* Item)
{
	FEquipmentGrant Grant;
	if (!ItemGrants.RemoveAndCopyValue(Item, Grant)) return;
	if (Item)
	{
		Item->OnDestroyed.RemoveDynamic(this, &ThisClass::OnGrantedItemDestroyed);
		Item->OnEndPlay.RemoveDynamic(this, &ThisClass::OnGrantedItemEndPlay);
	}
	if (UAbilitySystemComponent* ASC = Grant.ASC.Get())
	{
		for (const FGameplayAbilitySpecHandle Handle : Grant.Abilities)
		{
			ASC->CancelAbilityHandle(Handle);
			ASC->ClearAbility(Handle);
		}
		for (const FActiveGameplayEffectHandle Handle : Grant.Effects) ASC->RemoveActiveGameplayEffect(Handle);
	}
}

void UPlayerEquipmentComponent::OnGrantedItemDestroyed(AActor* Item)
{
	RemoveEquippedItemAbility(Cast<ABaseItem>(Item));
	if (PlayerOwner && PlayerOwner->EquippedItem == Item)
	{
		PlayerOwner->EquippedItem = nullptr;
		EquipmentState = EEquipmentState::None;
		PlayerOwner->SetCombatMode(false);
	}
}

void UPlayerEquipmentComponent::OnGrantedItemEndPlay(AActor* Item, EEndPlayReason::Type Reason)
{
	OnGrantedItemDestroyed(Item);
}

void UPlayerEquipmentComponent::BindOwnerAbilitySystem()
{
	UAbilitySystemComponent* ASC = PlayerOwner ? PlayerOwner->GetAbilitySystemComponent() : nullptr;
	if (!ASC || !PlayerOwner->HasAuthority()) return;
	if (DeathASC.Get() != ASC || !DeathDelegate.IsValid())
	{
		if (DeathASC.IsValid()) DeathASC->RegisterGameplayTagEvent(State_Dead, EGameplayTagEventType::NewOrRemoved).Remove(DeathDelegate);
		DeathASC = ASC;
		DeathDelegate = ASC->RegisterGameplayTagEvent(State_Dead, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &ThisClass::OnOwnerDead);
	}
}

void UPlayerEquipmentComponent::OnOwnerDead(FGameplayTag Tag, int32 Count)
{
	if (Count <= 0) return;
	CancelPendingEquip();
	StoreCurrentEquippedItem();
	EquipmentState = EEquipmentState::None;
}

void UPlayerEquipmentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PlayerOwner) PlayerOwner->OnAbilitySystemInitialized.RemoveAll(this);
	if (PendingPresentationItem.IsValid()) PendingPresentationItem->OnItemInitialized.RemoveAll(this);
	if (UAbilitySystemComponent* ASC = DeathASC.Get())
		ASC->RegisterGameplayTagEvent(State_Dead, EGameplayTagEventType::NewOrRemoved).Remove(DeathDelegate);
	TArray<TWeakObjectPtr<ABaseItem>> Items;
	ItemGrants.GetKeys(Items);
	for (const TWeakObjectPtr<ABaseItem>& Item : Items) RemoveEquippedItemAbility(Item.Get());
	Super::EndPlay(EndPlayReason);
}

bool UPlayerEquipmentComponent::AttachItem(ABaseItem* Item, EEquipmentAttachmentTarget Target)
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
		return CompleteItemAttachment(Item, Target, bAttached);
	}

	USceneComponent* GripComponent = Item->GetAttachmentReferenceComponent();

	if (!GripComponent || !GripComponent->DoesSocketExist(ItemGripSocketName) || !Item->GetRootComponent())
	{
		UE_LOG(LogTemp, Warning, TEXT("UPlayerEquipmentComponent::AttachItem : Item %s has no grip socket %s on its attachment reference component. Falling back to root attachment."), *GetNameSafe(Item), *ItemGripSocketName.ToString());
		const bool bAttached = Item->AttachToComponent(
			PlayerOwner->GetMesh(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			CharacterSocketName);
		return CompleteItemAttachment(Item, Target, bAttached);
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
	return CompleteItemAttachment(Item, Target, true);
}

bool UPlayerEquipmentComponent::CompleteItemAttachment(
	ABaseItem* Item,
	EEquipmentAttachmentTarget Target,
	bool bAttached)
{
	if (!bAttached)
	{
		return false;
	}

	ABowItem* Bow = Target == EEquipmentAttachmentTarget::Equipped
		? Cast<ABowItem>(Item)
		: nullptr;
	if (ABowItem* PreviouslyBoundBow = BoundBowArrowAnchor.Get(); PreviouslyBoundBow != Bow)
	{
		ClearBowArrowAnchor();
	}

	if (!Bow)
	{
		return true;
	}

	if (!Bow->BindArrowAnchor(PlayerOwner ? PlayerOwner->GetMesh() : nullptr))
	{
		UE_LOG(LogTemp, Error,
			TEXT("UPlayerEquipmentComponent::CompleteItemAttachment: Character mesh cannot resolve Bow socket %s for %s."),
			*Bow->GetCharacterArrowSocketName().ToString(),
			*GetNameSafe(Bow));
		ClearBowArrowAnchor(Bow);
		return false;
	}

	BoundBowArrowAnchor = Bow;
	return true;
}

void UPlayerEquipmentComponent::ClearBowArrowAnchor(ABowItem* ExpectedBow)
{
	ABowItem* BoundBow = BoundBowArrowAnchor.Get();
	if (ExpectedBow)
	{
		ExpectedBow->UnbindArrowAnchor();
		if (BoundBow == ExpectedBow)
		{
			BoundBowArrowAnchor.Reset();
		}
		return;
	}

	if (BoundBow)
	{
		BoundBow->UnbindArrowAnchor();
	}
	BoundBowArrowAnchor.Reset();
}

bool UPlayerEquipmentComponent::StoreCurrentEquippedItem(bool bRemoveStats)
{
	CancelActiveWeaponAbilities();

	if (!PlayerOwner || !IsValid(PlayerOwner->EquippedItem))
	{
		return true;
	}

	ABaseItem* PreviousItem = PlayerOwner->EquippedItem;
	ClearBowArrowAnchor(Cast<ABowItem>(PreviousItem));
	if (bRemoveStats)
	{
		auto* Stats = UEquipmentStatComponent::GetOrCreate(PlayerOwner);
		if (!Stats || !Stats->Clear()) return false;
	}
	RemoveEquippedItemAbility(PreviousItem);
	PlayerOwner->EquippedItem = nullptr;

	// Equipped actors are transient inventory representations.
	PreviousItem->Destroy();

	PlayerOwner->SetCombatMode(false);
	return true;
}

void UPlayerEquipmentComponent::StartEquipItem(ABaseItem* Item, FGameplayTag SourceSlotTag)
{
	if (!PlayerOwner || !PlayerOwner->HasAuthority() || !IsValid(Item) || !CanChangeEquipment())
	{
		return;
	}

	PendingEquipItem = Item;
	PendingEquipSlotTag = SourceSlotTag;
	EquipmentState = EEquipmentState::Equipping;

	const FWeaponAnimationEntry* Entry = ResolveWeaponAnimationEntry(Item);
	UAnimMontage* EquipMontage = Entry ? Entry->EquipMontage.Get() : nullptr;
	const float PlayRate = Entry ? Entry->EquipPlayRate : 1.f;

	const bool bIsSwimming = PlayerOwner->GetSwimmingComponent() && PlayerOwner->GetSwimmingComponent()->IsCustomSwimming();

	if (EquipMontage && !bIsSwimming)
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

	if (!CanChangeEquipment()) { CancelPendingEquip(); return; }
	ABaseItem* ItemToEquip = PendingEquipItem.Get();
	if (!IsValid(ItemToEquip))
	{
		CancelPendingEquip();
		return;
	}

	if (PlayerOwner->EquippedItem != ItemToEquip)
	{
		if (!ValidateWeapon(ItemToEquip))
		{
			UE_LOG(LogTemp, Warning, TEXT("Equipment rejected %s: invalid weapon definition, role, ability set or presentation."), *ItemToEquip->ItemTag.ToString());
			CancelPendingEquip();
			return;
		}
		ItemToEquip->SetItemState(EItemState::Equipped);
		if (!AttachItem(ItemToEquip, EEquipmentAttachmentTarget::Equipped))
		{
			UE_LOG(LogTemp, Error,
				TEXT("UPlayerEquipmentComponent::FinalizePendingEquip : Failed to attach item %s. Equip cancelled."),
				*GetNameSafe(ItemToEquip));
			CancelPendingEquip();
			return;
		}

		if (!GrantEquippedItemAbility(ItemToEquip)) { CancelPendingEquip(); return; }
		auto* Stats = UEquipmentStatComponent::GetOrCreate(PlayerOwner);
		if (!Stats || !Stats->Equip(PlayerOwner->GetAbilitySystemComponent(), ItemToEquip,
			ItemToEquip->GetStrengthBonus(), StrengthEquipmentEffectClass))
		{
			CancelPendingEquip();
			return;
		}
		StoreCurrentEquippedItem(false);
		PlayerOwner->EquippedItem = ItemToEquip;
		PlayerOwner->EnterCombatModeFromEquipment();
	}

	EquipmentState = EEquipmentState::Equipped;
	PendingEquipItem = nullptr;
	PendingEquipSlotTag = FGameplayTag();
	ActiveEquipmentMontage = nullptr;
	PlayerOwner->OnQuickSlotsChanged.Broadcast();
}

void UPlayerEquipmentComponent::CancelPendingEquip()
{
	ABaseItem* ItemToCancel = PendingEquipItem.Get();
	RemoveEquippedItemAbility(ItemToCancel);
	if (ItemToCancel)
	{
		ClearBowArrowAnchor(Cast<ABowItem>(ItemToCancel));
	}

	if (PlayerOwner && PlayerOwner->HasAuthority())
	{
		if (ItemToCancel)
		{
			if (ItemToCancel != PlayerOwner->EquippedItem)
			{
				ItemToCancel->Destroy();
			}
		}

		EquipmentState = IsValid(PlayerOwner->EquippedItem) ? EEquipmentState::Equipped : EEquipmentState::None;
	}

	PendingEquipItem = nullptr;
	PendingEquipSlotTag = FGameplayTag();
	ActiveEquipmentMontage = nullptr;
	if (ItemToCancel && PlayerOwner && IsValid(PlayerOwner->EquippedItem))
		AttachItem(PlayerOwner->EquippedItem, EEquipmentAttachmentTarget::Equipped);
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


bool UPlayerEquipmentComponent::CanChangeEquipment() const
{
	const UAbilitySystemComponent* ASC = PlayerOwner ? PlayerOwner->GetAbilitySystemComponent() : nullptr;
	return ASC && !ASC->HasMatchingGameplayTag(State_Dead) && !ASC->HasMatchingGameplayTag(State_Attacking)
		&& !ASC->HasMatchingGameplayTag(State_Bow_Drawing) && !ASC->HasMatchingGameplayTag(State_Bow_FullyDrawn)
		&& !ASC->HasMatchingGameplayTag(State_Bow_Releasing);
}
