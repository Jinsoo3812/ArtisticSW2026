#include "UI/SkillQuickSlotWidget.h"

#include "BaseGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "BasePlayer.h"
#include "Cannon.h"
#include "Ship.h"
#include "Components/Border.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Widget.h"
#include "Inventory/InventoryComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Skills/PlayerSkillComponent.h"

void USkillQuickSlotWidget::NativeConstruct()
{
	Super::NativeConstruct();
	InitializeForPlayer(Cast<ABasePlayer>(GetOwningPlayerPawn()));
}

void USkillQuickSlotWidget::NativeDestruct()
{
	UnbindPlayer();
	Super::NativeDestruct();
}

void USkillQuickSlotWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	if (GravityVortexLockOverlay)
	{
		GravityVortexLockOverlay->SetBrushColor(LockedOverlayColor);
	}
	if (WaterBombLockOverlay)
	{
		WaterBombLockOverlay->SetBrushColor(LockedOverlayColor);
	}
	if (BombardmentLockOverlay)
	{
		BombardmentLockOverlay->SetBrushColor(LockedOverlayColor);
	}
}

void USkillQuickSlotWidget::InitializeForPlayer(ABasePlayer* InPlayer)
{
	if (CachedPlayer.Get() != InPlayer)
	{
		UnbindPlayer();
		CachedPlayer = InPlayer;

		if (InPlayer)
		{
			if (UPlayerSkillComponent* SkillComponent = InPlayer->GetPlayerSkillComponent())
			{
				SkillComponent->OnSkillChanged.AddDynamic(this, &USkillQuickSlotWidget::HandleSkillChanged);
			}
			if (UInventoryComponent* Inventory = InPlayer->GetInventoryComponent())
			{
				Inventory->OnInventoryChanged.AddUObject(this, &USkillQuickSlotWidget::RefreshSlots);
			}
		}
	}

	BindSkillAbilitySystem();
	RefreshSlots();
}

void USkillQuickSlotWidget::HandleSkillChanged(const FGameplayTag)
{
	RefreshSlots();
}

void USkillQuickSlotWidget::UnbindPlayer()
{
	if (UAbilitySystemComponent* ASC = BoundSkillAbilitySystem.Get())
	{
		ASC->RegisterGameplayTagEvent(GameplayAbility_Skill_GravityVortex,
			EGameplayTagEventType::NewOrRemoved).Remove(GravityVortexTagChangedHandle);
	}
	BoundSkillAbilitySystem.Reset();
	GravityVortexTagChangedHandle.Reset();

	if (ABasePlayer* Player = CachedPlayer.Get())
	{
		if (UPlayerSkillComponent* SkillComponent = Player->GetPlayerSkillComponent())
		{
			SkillComponent->OnSkillChanged.RemoveAll(this);
		}
		if (UInventoryComponent* Inventory = Player->GetInventoryComponent())
		{
			Inventory->OnInventoryChanged.RemoveAll(this);
		}
	}

	CachedPlayer.Reset();
}

void USkillQuickSlotWidget::BindSkillAbilitySystem()
{
	UAbilitySystemComponent* ASC = CachedPlayer.IsValid()
		? CachedPlayer->GetAbilitySystemComponent() : nullptr;
	if (BoundSkillAbilitySystem.Get() == ASC)
	{
		return;
	}
	if (UAbilitySystemComponent* PreviousASC = BoundSkillAbilitySystem.Get())
	{
		PreviousASC->RegisterGameplayTagEvent(GameplayAbility_Skill_GravityVortex,
			EGameplayTagEventType::NewOrRemoved).Remove(GravityVortexTagChangedHandle);
	}
	GravityVortexTagChangedHandle.Reset();
	BoundSkillAbilitySystem = ASC;
	if (ASC)
	{
		GravityVortexTagChangedHandle = ASC->RegisterGameplayTagEvent(
			GameplayAbility_Skill_GravityVortex, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &USkillQuickSlotWidget::HandleActiveSkillTagChanged);
	}
}

void USkillQuickSlotWidget::HandleActiveSkillTagChanged(FGameplayTag, int32)
{
	APawn* ControlledPawn = GetOwningPlayerPawn();
	RefreshEquippedState(ControlledPawn ? ControlledPawn : CachedPlayer.Get());
}

void USkillQuickSlotWidget::RefreshSlots()
{
	RefreshSkill(GameplayAbility_Skill_GravityVortex, GravityVortexIconImage, GravityVortexLockOverlay);
	RefreshSkill(GameplayAbility_Skill_WaterBomb, WaterBombIconImage, WaterBombLockOverlay);
	RefreshSkill(GameplayAbility_Skill_Bombardment, BombardmentIconImage, BombardmentLockOverlay);
	RefreshEquippedState(GetOwningPlayerPawn());
}

void USkillQuickSlotWidget::RefreshEquippedState(APawn* ControlledPawn)
{
	FrontSkillTag = Cast<ACannon>(ControlledPawn) ? GameplayAbility_Skill_WaterBomb
		: Cast<AShip>(ControlledPawn) ? GameplayAbility_Skill_Bombardment
		: GameplayAbility_Skill_GravityVortex;

	UWidget* Panels[] = { GravityVortexSlotPanel, WaterBombSlotPanel, BombardmentSlotPanel };
	const FGameplayTag Tags[] =
	{
		GameplayAbility_Skill_GravityVortex, GameplayAbility_Skill_WaterBomb, GameplayAbility_Skill_Bombardment
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Panels); ++Index)
	{
		if (UCanvasPanelSlot* CanvasSlot = Panels[Index] ? Cast<UCanvasPanelSlot>(Panels[Index]->Slot) : nullptr)
		{
			CanvasSlot->SetZOrder(Tags[Index] == FrontSkillTag ? UE_ARRAY_COUNT(Panels) : Index);
		}
	}

	const ABasePlayer* Player = CachedPlayer.Get();
	const UAbilitySystemComponent* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
	if (GravityVortexSelectedOverlay)
	{
		const bool bSelected = ControlledPawn == Player && ASC
			&& ASC->HasMatchingGameplayTag(GameplayAbility_Skill_GravityVortex);
		GravityVortexSelectedOverlay->SetVisibility(bSelected
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	if (WaterBombSelectedOverlay)
	{
		const ACannon* Cannon = Cast<ACannon>(ControlledPawn);
		WaterBombSelectedOverlay->SetVisibility(Cannon && Cannon->IsWaterBombMode()
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	if (BombardmentSelectedOverlay)
	{
		const AShip* Ship = Cast<AShip>(ControlledPawn);
		BombardmentSelectedOverlay->SetVisibility(Ship && Ship->IsBombardmentTargeting()
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
}

void USkillQuickSlotWidget::RefreshSkill(
	const FGameplayTag SkillTag,
	UImage* IconImage,
	UBorder* LockOverlay) const
{
	ABasePlayer* Player = CachedPlayer.Get();
	UPlayerSkillComponent* SkillComponent = Player ? Player->GetPlayerSkillComponent() : nullptr;
	UInventoryComponent* Inventory = Player ? Player->GetInventoryComponent() : nullptr;
	const FPlayerSkillDefinition* Definition = SkillComponent
		? SkillComponent->FindSkillDefinition(SkillTag)
		: nullptr;

	if (IconImage)
	{
		UTexture2D* Icon = Inventory && Definition && Definition->SkillItemTag.IsValid()
			? Inventory->GetMaterialIcon(Definition->SkillItemTag)
			: nullptr;
		IconImage->SetBrushFromTexture(Icon, true);
		IconImage->SetColorAndOpacity(Icon ? FLinearColor::White : FLinearColor::Transparent);
	}

	if (LockOverlay)
	{
		const bool bCanUseSkill = Player && Player->CanUseSkill(SkillTag);
		LockOverlay->SetBrushColor(LockedOverlayColor);
		LockOverlay->SetVisibility(bCanUseSkill
			? ESlateVisibility::Hidden
			: ESlateVisibility::HitTestInvisible);
	}
}

void USkillQuickSlotWidget::SetSkillCooldown(
	const FGameplayTag SkillTag,
	const float RemainingSeconds,
	const float DurationSeconds)
{
	if (UImage* CooldownImage = FindCooldownImage(SkillTag))
	{
		const float Percent = DurationSeconds > KINDA_SMALL_NUMBER
			? FMath::Clamp(RemainingSeconds / DurationSeconds, 0.0f, 1.0f)
			: 0.0f;
		if (UMaterialInstanceDynamic* CooldownMaterial = CooldownImage->GetDynamicMaterial())
		{
			CooldownMaterial->SetScalarParameterValue(CooldownPercentParameterName, Percent);
		}
	}
}

UImage* USkillQuickSlotWidget::FindCooldownImage(const FGameplayTag SkillTag) const
{
	if (SkillTag.MatchesTagExact(GameplayAbility_Skill_GravityVortex))
	{
		return GravityVortexCooldownImage;
	}
	if (SkillTag.MatchesTagExact(GameplayAbility_Skill_WaterBomb))
	{
		return WaterBombCooldownImage;
	}
	if (SkillTag.MatchesTagExact(GameplayAbility_Skill_Bombardment))
	{
		return BombardmentCooldownImage;
	}
	return nullptr;
}
