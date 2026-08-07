#include "UI/SkillQuickSlotWidget.h"

#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Cannon.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Inventory/InventoryComponent.h"
#include "Skills/PlayerSkillComponent.h"
#include "Ship.h"
#include "UObject/ConstructorHelpers.h"

USkillQuickSlotWidget::USkillQuickSlotWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UTexture2D> LockTextureFinder(
		TEXT("/Game/Blueprints/02_UI/UI_HUD/UI_SkillQuickSlot/T_SkillLock.T_SkillLock"));
	if (LockTextureFinder.Succeeded())
	{
		StoryLockTexture = LockTextureFinder.Object;
	}
}

void USkillQuickSlotWidget::InitializeForPlayer(ABasePlayer* InPlayer)
{
	UnbindActiveSkillTag();
	CachedPlayer = InPlayer;
	BindActiveSkillTag();
	RefreshSlot();
}

void USkillQuickSlotWidget::RefreshSlot()
{
	const FKey DisplayKey = ResolveDisplayKey();
	if (InputKeyText)
	{
		InputKeyText->SetText(DisplayKey.IsValid() ? DisplayKey.GetDisplayName(false) : FText::GetEmpty());
	}

	ABasePlayer* Player = CachedPlayer.Get();
	UPlayerSkillComponent* SkillComponent = Player ? Player->GetPlayerSkillComponent() : nullptr;
	UInventoryComponent* Inventory = Player ? Player->GetInventoryComponent() : nullptr;
	const FPlayerSkillDefinition* Definition =
		SkillComponent && SkillTag.IsValid() ? SkillComponent->FindSkillDefinition(SkillTag) : nullptr;

	const int32 UseCount = SkillComponent && Definition
		? SkillComponent->GetSkillUseCount(SkillTag)
		: 0;
	const bool bStoryUnlocked = SkillComponent && Definition && SkillComponent->IsSkillUnlocked(SkillTag);
	const bool bHasUses = UseCount > 0;
	const bool bShowLock = !bStoryUnlocked && !bHasUses;
	if (UseCountText)
	{
		UseCountText->SetText(FText::AsNumber(UseCount));
	}
	if (EmptyOverlayBorder)
	{
		EmptyOverlayBorder->SetVisibility(!bHasUses ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	RefreshEquippedState(GetOwningPlayerPawn());
	if (StoryLockImage)
	{
		StoryLockImage->SetVisibility(bShowLock ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		StoryLockImage->SetBrushFromTexture(StoryLockTexture, true);
	}

	UTexture2D* SkillIcon = nullptr;
	if (Inventory && Definition && Definition->SkillItemTag.IsValid())
	{
		SkillIcon = Inventory->GetMaterialIcon(Definition->SkillItemTag);
	}

	if (SkillIconImage && SkillIcon)
	{
		SkillIconImage->SetBrushFromTexture(SkillIcon, true);
		SkillIconImage->SetColorAndOpacity(bHasUses ? AvailableIconColor : UnavailableIconColor);
		SkillIconImage->SetIsEnabled(bHasUses);
	}
	else if (SkillIconImage)
	{
		SkillIconImage->SetBrushFromTexture(nullptr);
		SkillIconImage->SetColorAndOpacity(FLinearColor::Transparent);
	}
}

void USkillQuickSlotWidget::RefreshEquippedState(APawn* ControlledPawn)
{
	if (!EquippedBorder)
	{
		return;
	}

	bool bSkillActive = false;
	if (SkillTag.MatchesTagExact(GameplayAbility_Skill_WaterBomb))
	{
		const ACannon* ControlledCannon = Cast<ACannon>(ControlledPawn);
		bSkillActive = ControlledCannon && ControlledCannon->IsWaterBombMode();
	}
	else if (SkillTag.MatchesTagExact(GameplayAbility_Skill_Bombardment))
	{
		const AShip* ControlledShip = Cast<AShip>(ControlledPawn);
		bSkillActive = ControlledShip && ControlledShip->IsBombardmentTargeting();
	}
	else
	{
		const ABasePlayer* Player = CachedPlayer.Get();
		const UAbilitySystemComponent* AbilitySystemComponent =
			Player ? Player->GetAbilitySystemComponent() : nullptr;
		bSkillActive =
			AbilitySystemComponent && SkillTag.IsValid()
			&& AbilitySystemComponent->HasMatchingGameplayTag(SkillTag);
	}

	const ESlateVisibility DesiredVisibility =
		bSkillActive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden;
	if (EquippedBorder->GetVisibility() != DesiredVisibility)
	{
		EquippedBorder->SetVisibility(DesiredVisibility);
	}
}

void USkillQuickSlotWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshSlot();
}

void USkillQuickSlotWidget::NativeDestruct()
{
	UnbindActiveSkillTag();
	Super::NativeDestruct();
}

void USkillQuickSlotWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	if (CachedPlayer.IsValid() && BoundActiveSkillTag != SkillTag)
	{
		UnbindActiveSkillTag();
		BindActiveSkillTag();
	}

	RefreshSlot();
}

void USkillQuickSlotWidget::BindActiveSkillTag()
{
	ABasePlayer* Player = CachedPlayer.Get();
	UAbilitySystemComponent* AbilitySystemComponent = Player ? Player->GetAbilitySystemComponent() : nullptr;
	if (!AbilitySystemComponent || !SkillTag.IsValid())
	{
		return;
	}

	BoundAbilitySystemComponent = AbilitySystemComponent;
	BoundActiveSkillTag = SkillTag;
	ActiveSkillTagEventHandle =
		AbilitySystemComponent
			->RegisterGameplayTagEvent(BoundActiveSkillTag, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &USkillQuickSlotWidget::HandleActiveSkillTagChanged);
}

void USkillQuickSlotWidget::UnbindActiveSkillTag()
{
	if (UAbilitySystemComponent* AbilitySystemComponent = BoundAbilitySystemComponent.Get();
		AbilitySystemComponent && BoundActiveSkillTag.IsValid() && ActiveSkillTagEventHandle.IsValid())
	{
		AbilitySystemComponent
			->RegisterGameplayTagEvent(BoundActiveSkillTag, EGameplayTagEventType::NewOrRemoved)
			.Remove(ActiveSkillTagEventHandle);
	}

	BoundAbilitySystemComponent.Reset();
	BoundActiveSkillTag = FGameplayTag();
	ActiveSkillTagEventHandle.Reset();
}

void USkillQuickSlotWidget::HandleActiveSkillTagChanged(const FGameplayTag, int32)
{
	RefreshSlot();
}

FKey USkillQuickSlotWidget::ResolveDisplayKey() const
{
	if (InputKey.IsValid())
	{
		return InputKey;
	}
	if (SkillTag.MatchesTagExact(GameplayAbility_Skill_GravityVortex))
	{
		return EKeys::Three;
	}
	if (SkillTag.MatchesTagExact(GameplayAbility_Skill_WaterBomb))
	{
		return EKeys::Four;
	}
	if (SkillTag.MatchesTagExact(GameplayAbility_Skill_Bombardment))
	{
		return EKeys::Five;
	}
	return FKey();
}
