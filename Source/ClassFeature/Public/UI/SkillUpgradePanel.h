#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "UI/SkillUpgradeTypes.h"
#include "SkillUpgradePanel.generated.h"

class UBorder;
class UImage;
class UPlayerSkillComponent;
class UTextBlock;

/**
 * Logic parent for WBP_SkillUpgradePanel.
 * WBP owns imagery and styling; C++ owns selection and replicated lock-state display.
 */
UCLASS(Blueprintable)
class CLASSFEATURE_API USkillUpgradePanel : public UUserWidget
{
	GENERATED_BODY()

public:
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

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

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

	/** Use this event in WBP to replace Image_Skill for the selected skill. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Skill Upgrade", meta = (DisplayName = "On Skill Upgrade Selection Changed"))
	void BP_OnSkillUpgradeSelectionChanged(ESkillUpgradeSelection NewSelection);

private:
	void ResolveSkillComponent();
	void UnbindSkillComponent();
	FGameplayTag GetSelectedSkillTag() const;
	FText GetSelectedUnlockConditionText() const;

	UFUNCTION()
	void HandleSkillChanged(FGameplayTag ChangedSkillTag);

	UPROPERTY(Transient)
	TObjectPtr<UPlayerSkillComponent> SkillComponent;

	ESkillUpgradeSelection SelectedSkill = ESkillUpgradeSelection::GravityVortex;
	bool bHasSelectedSkill = false;
};
