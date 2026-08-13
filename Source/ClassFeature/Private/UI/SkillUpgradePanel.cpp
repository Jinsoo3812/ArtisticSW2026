#include "UI/SkillUpgradePanel.h"

#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Skills/PlayerSkillComponent.h"

void USkillUpgradePanel::NativeConstruct()
{
	Super::NativeConstruct();

	ResolveSkillComponent();
	RefreshLockState();
}

void USkillUpgradePanel::NativeDestruct()
{
	UnbindSkillComponent();
	Super::NativeDestruct();
}

void USkillUpgradePanel::SetSelectedSkill(ESkillUpgradeSelection InSelectedSkill)
{
	SelectedSkill = InSelectedSkill;
	bHasSelectedSkill = true;
	ResolveSkillComponent();
	BP_OnSkillUpgradeSelectionChanged(SelectedSkill);
	RefreshLockState();
}

void USkillUpgradePanel::RefreshLockState()
{
	const bool bUnlocked = IsSelectedSkillUnlocked();
	const ESlateVisibility LockVisibility =
		bHasSelectedSkill && !bUnlocked
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed;

	if (Border_LockOverlay)
	{
		Border_LockOverlay->SetVisibility(LockVisibility);
	}
	if (Image_Lock)
	{
		Image_Lock->SetVisibility(LockVisibility);
	}
	if (Text_UnlockCondition)
	{
		Text_UnlockCondition->SetText(GetSelectedUnlockConditionText());
		Text_UnlockCondition->SetVisibility(LockVisibility);
	}
}

bool USkillUpgradePanel::IsSelectedSkillUnlocked() const
{
	return bHasSelectedSkill
		&& SkillComponent
		&& SkillComponent->IsSkillUnlocked(GetSelectedSkillTag());
}

void USkillUpgradePanel::ResolveSkillComponent()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetOwningPlayerPawn());
	UPlayerSkillComponent* ResolvedComponent = Player ? Player->GetPlayerSkillComponent() : nullptr;
	if (SkillComponent == ResolvedComponent)
	{
		return;
	}

	UnbindSkillComponent();
	SkillComponent = ResolvedComponent;
	if (SkillComponent)
	{
		SkillComponent->OnSkillChanged.AddUniqueDynamic(this, &USkillUpgradePanel::HandleSkillChanged);
	}
}

void USkillUpgradePanel::UnbindSkillComponent()
{
	if (SkillComponent)
	{
		SkillComponent->OnSkillChanged.RemoveDynamic(this, &USkillUpgradePanel::HandleSkillChanged);
		SkillComponent = nullptr;
	}
}

FGameplayTag USkillUpgradePanel::GetSelectedSkillTag() const
{
	switch (SelectedSkill)
	{
	case ESkillUpgradeSelection::GravityVortex:
		return GameplayAbility_Skill_GravityVortex;
	case ESkillUpgradeSelection::WaterBomb:
		return GameplayAbility_Skill_WaterBomb;
	case ESkillUpgradeSelection::Bombardment:
		return GameplayAbility_Skill_Bombardment;
	default:
		return FGameplayTag();
	}
}

FText USkillUpgradePanel::GetSelectedUnlockConditionText() const
{
	switch (SelectedSkill)
	{
	case ESkillUpgradeSelection::GravityVortex:
		return GravityVortexUnlockConditionText;
	case ESkillUpgradeSelection::WaterBomb:
		return WaterBombUnlockConditionText;
	case ESkillUpgradeSelection::Bombardment:
		return BombardmentUnlockConditionText;
	default:
		return FText::GetEmpty();
	}
}

void USkillUpgradePanel::HandleSkillChanged(FGameplayTag ChangedSkillTag)
{
	if (bHasSelectedSkill && ChangedSkillTag.MatchesTagExact(GetSelectedSkillTag()))
	{
		RefreshLockState();
	}
}
