#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "SkillQuickSlotWidget.generated.h"

class ABasePlayer;
class UBorder;
class UImage;
class UWidget;

/**
 * Designer-placeable container for every player skill quick slot.
 *
 * WBP_SkillQuickSlot owns the layout. Place the three *SlotPanel widgets as direct
 * children of a Canvas Panel. Input ownership belongs to the active pawn mode;
 * this widget only presents skill availability and cooldown state.
 */
UCLASS(Blueprintable)
class CLASSFEATURE_API USkillQuickSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeForPlayer(ABasePlayer* InPlayer);

	UFUNCTION(BlueprintCallable, Category = "Skill Quick Slot")
	void RefreshSlots();

	/** Display-only API. It does not apply or enforce gameplay cooldowns. */
	UFUNCTION(BlueprintCallable, Category = "Skill Quick Slot|Cooldown")
	void SetSkillCooldown(FGameplayTag SkillTag, float RemainingSeconds, float DurationSeconds);

	UFUNCTION(BlueprintPure, Category = "Skill Quick Slot")
	FGameplayTag GetFrontSkillTag() const { return FrontSkillTag; }

	/** Kept for HUD compatibility; active-state borders are no longer part of this widget. */
	void RefreshEquippedState(APawn* ControlledPawn) {}

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void SynchronizeProperties() override;

	/** Semi-transparent cover placed above a locked skill's contents. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skill Quick Slot|Style")
	FLinearColor LockedOverlayColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.55f);

	/** Scalar parameter read by the circular cooldown UI materials. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skill Quick Slot|Cooldown")
	FName CooldownPercentParameterName = TEXT("Percent");


	/** Panel component: GravityVortexSlotPanel (direct SkillSlotCanvas child). */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> GravityVortexSlotPanel;

	/** Image component: GravityVortexIconImage. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> GravityVortexIconImage;

	/** Image component: GravityVortexCooldownImage (circular UI material brush). */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> GravityVortexCooldownImage;

	/** Border component: GravityVortexLockOverlay. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> GravityVortexLockOverlay;

	/** Panel component: WaterBombSlotPanel (direct SkillSlotCanvas child). */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> WaterBombSlotPanel;

	/** Image component: WaterBombIconImage. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> WaterBombIconImage;

	/** Image component: WaterBombCooldownImage (circular UI material brush). */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> WaterBombCooldownImage;

	/** Border component: WaterBombLockOverlay. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> WaterBombLockOverlay;

	/** Panel component: BombardmentSlotPanel (direct SkillSlotCanvas child). */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> BombardmentSlotPanel;

	/** Image component: BombardmentIconImage. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> BombardmentIconImage;

	/** Image component: BombardmentCooldownImage (circular UI material brush). */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> BombardmentCooldownImage;

	/** Border component: BombardmentLockOverlay. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BombardmentLockOverlay;

private:
	UFUNCTION()
	void HandleSkillChanged(FGameplayTag SkillTag);

	void UnbindPlayer();
	void RefreshSkill(FGameplayTag SkillTag, UImage* IconImage, UBorder* LockOverlay) const;
	UImage* FindCooldownImage(FGameplayTag SkillTag) const;

	TWeakObjectPtr<ABasePlayer> CachedPlayer;
	FGameplayTag FrontSkillTag;
};
