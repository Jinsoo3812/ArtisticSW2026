#include "UI/SkillQuickSlotWidget.h"

#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Components/Border.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/InventoryComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Skills/PlayerSkillComponent.h"

namespace
{
UCanvasPanelSlot* FindCanvasLayerSlot(UWidget* Widget)
{
	for (UWidget* Current = Widget; Current; Current = Current->GetParent())
	{
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Current->Slot))
		{
			return CanvasSlot;
		}
	}

	return nullptr;
}

const UCanvasPanelSlot* FindCanvasLayerSlot(const UWidget* Widget)
{
	for (const UWidget* Current = Widget; Current; Current = Current->GetParent())
	{
		if (const UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Current->Slot))
		{
			return CanvasSlot;
		}
	}

	return nullptr;
}
}

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

void USkillQuickSlotWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshInputState();
}

void USkillQuickSlotWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	RefreshInputLabels();

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

	NextSlotZOrder = ResolveHighestSlotZOrder();
	InitializeInputState();
	RefreshSlots();
}

void USkillQuickSlotWidget::HandleSkillChanged(const FGameplayTag)
{
	RefreshSlots();
}

void USkillQuickSlotWidget::UnbindPlayer()
{
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

void USkillQuickSlotWidget::RefreshSlots()
{
	RefreshInputLabels();
	RefreshSkill(GameplayAbility_Skill_GravityVortex, GravityVortexIconImage, GravityVortexLockOverlay);
	RefreshSkill(GameplayAbility_Skill_WaterBomb, WaterBombIconImage, WaterBombLockOverlay);
	RefreshSkill(GameplayAbility_Skill_Bombardment, BombardmentIconImage, BombardmentLockOverlay);
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
		const bool bUnlocked = SkillComponent && SkillComponent->IsSkillUnlocked(SkillTag);
		LockOverlay->SetBrushColor(LockedOverlayColor);
		LockOverlay->SetVisibility(bUnlocked
			? ESlateVisibility::Hidden
			: ESlateVisibility::HitTestInvisible);
	}
}

void USkillQuickSlotWidget::RefreshInputLabels() const
{
	auto SetKeyText = [](UTextBlock* TextBlock, const FKey& Key)
	{
		if (TextBlock)
		{
			TextBlock->SetText(Key.IsValid() ? Key.GetDisplayName(false) : FText::GetEmpty());
		}
	};

	SetKeyText(GravityVortexInputKeyText, GravityVortexInputKey);
	SetKeyText(WaterBombInputKeyText, WaterBombInputKey);
	SetKeyText(BombardmentInputKeyText, BombardmentInputKey);
}

void USkillQuickSlotWidget::InitializeInputState()
{
	const APlayerController* PlayerController = GetOwningPlayer();
	bGravityVortexKeyWasDown = PlayerController && PlayerController->IsInputKeyDown(GravityVortexInputKey);
	bWaterBombKeyWasDown = PlayerController && PlayerController->IsInputKeyDown(WaterBombInputKey);
	bBombardmentKeyWasDown = PlayerController && PlayerController->IsInputKeyDown(BombardmentInputKey);
}

void USkillQuickSlotWidget::RefreshInputState()
{
	const APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController)
	{
		return;
	}

	auto PromoteOnPress = [this, PlayerController](
		const FKey& Key,
		bool& bWasDown,
		const FGameplayTag SkillTag,
		UWidget* SlotPanel)
	{
		const bool bIsDown = Key.IsValid() && PlayerController->IsInputKeyDown(Key);
		if (bIsDown && !bWasDown)
		{
			PromoteSkill(SkillTag, SlotPanel);
		}
		bWasDown = bIsDown;
	};

	PromoteOnPress(
		GravityVortexInputKey,
		bGravityVortexKeyWasDown,
		GameplayAbility_Skill_GravityVortex,
		GravityVortexSlotPanel);
	PromoteOnPress(
		WaterBombInputKey,
		bWaterBombKeyWasDown,
		GameplayAbility_Skill_WaterBomb,
		WaterBombSlotPanel);
	PromoteOnPress(
		BombardmentInputKey,
		bBombardmentKeyWasDown,
		GameplayAbility_Skill_Bombardment,
		BombardmentSlotPanel);
}

void USkillQuickSlotWidget::PromoteSkill(const FGameplayTag SkillTag, UWidget* SlotPanel)
{
	if (!SlotPanel)
	{
		return;
	}

	if (UCanvasPanelSlot* CanvasSlot = FindCanvasLayerSlot(SlotPanel))
	{
		CanvasSlot->SetZOrder(++NextSlotZOrder);
		FrontSkillTag = SkillTag;
	}
}

int32 USkillQuickSlotWidget::ResolveHighestSlotZOrder() const
{
	int32 HighestZOrder = 0;
	const UWidget* SlotPanels[] = { GravityVortexSlotPanel, WaterBombSlotPanel, BombardmentSlotPanel };
	for (const UWidget* SlotPanel : SlotPanels)
	{
		if (const UCanvasPanelSlot* CanvasSlot = FindCanvasLayerSlot(SlotPanel))
		{
			HighestZOrder = FMath::Max(HighestZOrder, CanvasSlot->GetZOrder());
		}
	}
	return HighestZOrder;
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
