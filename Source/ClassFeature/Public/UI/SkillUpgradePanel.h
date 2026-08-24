#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Crafting/CraftingTypes.h"
#include "GameplayTagContainer.h"
#include "UI/SkillUpgradeTypes.h"
#include "SkillUpgradePanel.generated.h"

class UBorder;
class UButton;
class UCraftingCompleteWidget;
class UCraftingComponent;
class UCraftingIngredientEntryWidget;
class UImage;
class UPanelWidget;
class UPlayerSkillComponent;
class UTextBlock;
class UWidget;
class UWidgetSwitcher;

/**
 * Logic parent for WBP_SkillUpgradePanel.
 * WBP owns imagery and styling; C++ owns selection and replicated lock-state display.
 */
UCLASS(Blueprintable)
class CLASSFEATURE_API USkillUpgradePanel : public UUserWidget
{
	GENERATED_BODY()

public:
	void ActivateSkillCraftingPanel(UCraftingComponent* InCraftingComponent);
	void DeactivateSkillCraftingPanel();

	UFUNCTION(BlueprintCallable, Category = "Skill Upgrade")
	void SetSelectedSkill(ESkillUpgradeSelection InSelectedSkill);

	UFUNCTION(BlueprintCallable, Category = "Skill Upgrade")
	void RefreshLockState();

	UFUNCTION(BlueprintPure, Category = "Skill Upgrade")
	ESkillUpgradeSelection GetSelectedSkill() const { return SelectedSkill; }

	UFUNCTION(BlueprintPure, Category = "Skill Upgrade")
	bool HasSelectedSkill() const { return bHasSelectedSkill; }

	UFUNCTION(BlueprintPure, Category = "Skill Upgrade")
	bool IsSelectedSkillUnlocked() const;

	UFUNCTION(BlueprintPure, Category = "Skill Upgrade")
	bool IsSelectedSkillUnlockConditionMet() const;

	UFUNCTION(BlueprintPure, Category = "Skill Upgrade")
	ESkillCraftingUIState GetSkillCraftingState() const { return CraftingState; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** index 0: selected skill recipe, index 1: WBP_CraftingComplete. */
	UPROPERTY(BlueprintReadOnly, Category = "Skill Upgrade|Crafting", meta = (BindWidgetOptional))
	TObjectPtr<UWidgetSwitcher> WidgetSwitcher_SkillCraftingState;

	/** Designer-authored anchor that defines the convergence target. */
	UPROPERTY(BlueprintReadOnly, Category = "Skill Upgrade|Crafting", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> CraftingCenterAnchor;

	/** Designer-authored material hosts. Slot 1/2/3 correspond to 2/6/10 o'clock. */
	UPROPERTY(BlueprintReadOnly, Category = "Skill Upgrade|Crafting", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> IngredientSlot1;

	UPROPERTY(BlueprintReadOnly, Category = "Skill Upgrade|Crafting", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> IngredientSlot2;

	UPROPERTY(BlueprintReadOnly, Category = "Skill Upgrade|Crafting", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> IngredientSlot3;

	UPROPERTY(BlueprintReadOnly, Category = "Skill Upgrade|Crafting", meta = (BindWidgetOptional))
	TObjectPtr<UCraftingIngredientEntryWidget> IngredientEntry1;

	UPROPERTY(BlueprintReadOnly, Category = "Skill Upgrade|Crafting", meta = (BindWidgetOptional))
	TObjectPtr<UCraftingIngredientEntryWidget> IngredientEntry2;

	UPROPERTY(BlueprintReadOnly, Category = "Skill Upgrade|Crafting", meta = (BindWidgetOptional))
	TObjectPtr<UCraftingIngredientEntryWidget> IngredientEntry3;

	UPROPERTY(BlueprintReadOnly, Category = "Skill Upgrade|Crafting", meta = (BindWidgetOptional))
	TObjectPtr<UButton> CraftButton;

	UPROPERTY(BlueprintReadOnly, Category = "Skill Upgrade|Crafting", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CraftButtonText;

	UPROPERTY(BlueprintReadOnly, Category = "Skill Upgrade|Crafting", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_LockedMessage;

	UPROPERTY(BlueprintReadOnly, Category = "Skill Upgrade|Crafting", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SkillNameText;

	UPROPERTY(BlueprintReadOnly, Category = "Skill Upgrade|Crafting", meta = (BindWidgetOptional))
	TObjectPtr<UImage> ResultSkillIconImage;

	UPROPERTY(BlueprintReadOnly, Category = "Skill Upgrade|Crafting", meta = (BindWidgetOptional))
	TObjectPtr<UCraftingCompleteWidget> SkillCraftingCompleteWidget;

	/** Main skill artwork. C++ leaves its brush unchanged for WBP to author. */
	UPROPERTY(BlueprintReadOnly, Category = "Skill Upgrade", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_Skill;

	/** Semi-transparent dim layer placed above Image_Skill. */
	UPROPERTY(BlueprintReadOnly, Category = "Skill Upgrade", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> Border_LockOverlay;

	/** Lock artwork placed above Border_LockOverlay. */
	UPROPERTY(BlueprintReadOnly, Category = "Skill Upgrade", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_Lock;

	UPROPERTY(BlueprintReadOnly, Category = "Skill Upgrade", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_UnlockCondition;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skill Upgrade|Unlock Conditions", meta = (MultiLine = "true"))
	FText GravityVortexUnlockConditionText;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skill Upgrade|Unlock Conditions", meta = (MultiLine = "true"))
	FText WaterBombUnlockConditionText;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skill Upgrade|Unlock Conditions", meta = (MultiLine = "true"))
	FText BombardmentUnlockConditionText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skill Upgrade|Animation", meta = (ClampMin = "0.1"))
	float ConvergenceDuration = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skill Upgrade|Animation", meta = (ClampMin = "0.0"))
	float ConvergenceRevolutions = 1.5f;

	/** Use this event in WBP to replace Image_Skill for the selected skill. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Skill Upgrade", meta = (DisplayName = "On Skill Upgrade Selection Changed"))
	void BP_OnSkillUpgradeSelectionChanged(ESkillUpgradeSelection NewSelection);

private:
	void ResolveSkillComponent();
	void UnbindSkillComponent();
	void BindCraftingEvents();
	void UnbindCraftingEvents();
	void RefreshSelectedSkillPresentation();
	void RefreshCraftingState();
	void ApplyCraftingDetails(const FCraftingDetailsView& Details);
	void ClearIngredientEntries();
	void SetCraftButtonAvailable(bool bAvailable);
	void SetCraftingState(ESkillCraftingUIState NewState);
	void BeginConvergenceAnimation();
	void UpdateConvergenceAnimation(float DeltaTime);
	void FinishConvergenceAnimation();
	void ResetIngredientRenderTransforms();
	FGameplayTag GetSelectedSkillTag() const;
	FGameplayTag GetSelectedSkillItemTag() const;
	FText GetSelectedUnlockConditionText() const;

	UFUNCTION()
	void HandleSkillChanged(FGameplayTag ChangedSkillTag);

	UFUNCTION()
	void HandleCraftingDataChanged();

	UFUNCTION()
	void HandleCraftButtonClicked();

	UFUNCTION()
	void HandleCraftingResult(const FCraftingResult& Result);

	void HandleCraftingCompleteDismissed();

	UPROPERTY(Transient)
	TObjectPtr<UPlayerSkillComponent> SkillComponent;

	UPROPERTY(Transient)
	TObjectPtr<UCraftingComponent> CraftingComponent;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCraftingIngredientEntryWidget>> SpawnedIngredientEntries;

	ESkillUpgradeSelection SelectedSkill = ESkillUpgradeSelection::GravityVortex;
	ESkillCraftingUIState CraftingState = ESkillCraftingUIState::NoSelection;
	FCraftingListEntry SelectedRecipeHeader;
	FName SelectedRecipeId;
	FGuid PendingRequestId;
	TArray<FVector2D> IngredientStartOffsets;
	float ConvergenceElapsed = 0.0f;
	bool bHasSelectedSkill = false;
	bool bPanelActive = false;
	bool bCraftRequestPending = false;
	bool bConvergenceAnimating = false;
};
