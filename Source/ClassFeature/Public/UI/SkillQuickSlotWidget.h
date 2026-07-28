#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "InputCoreTypes.h"
#include "SkillQuickSlotWidget.generated.h"

class ABasePlayer;
class UAbilitySystemComponent;
class UBorder;
class UImage;
class UTextBlock;
class UTexture2D;

/**
 * A designer-placeable, always-visible skill slot.
 *
 * SkillTag and InputKey are instance-editable so the HUD Blueprint controls
 * slot order, placement, and displayed hotkey without changing C++.
 */
UCLASS(Blueprintable)
class CLASSFEATURE_API USkillQuickSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	USkillQuickSlotWidget(const FObjectInitializer& ObjectInitializer);

	void InitializeForPlayer(ABasePlayer* InPlayer);

	UFUNCTION(BlueprintCallable, Category = "Skill Quick Slot")
	void RefreshSlot();

	/** GameplayAbility.Skill.* represented by this designer-placed slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skill Quick Slot",
		meta = (ExposeOnSpawn = "true", DisplayName = "Skill Tag", Categories = "GameplayAbility.Skill"))
	FGameplayTag SkillTag;

	/** Key label shown in this slot. Defaults to 6/7/8 from SkillTag when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skill Quick Slot",
		meta = (ExposeOnSpawn = "true", DisplayName = "Input Key"))
	FKey InputKey;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void SynchronizeProperties() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skill Quick Slot|Style")
	FLinearColor AvailableIconColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skill Quick Slot|Style")
	FLinearColor UnavailableIconColor = FLinearColor(0.42f, 0.42f, 0.42f, 0.65f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skill Quick Slot|Style")
	TObjectPtr<UTexture2D> StoryLockTexture;

	/** Image component: SkillIconImage */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> SkillIconImage;

	/** Border component: EmptyOverlayBorder */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> EmptyOverlayBorder;

	/** Border component: EquippedBorder */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> EquippedBorder;

	/** Image component: StoryLockImage */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> StoryLockImage;

	/** Text component: InputKeyText */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> InputKeyText;

	/** Text component: UseCountText */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> UseCountText;

private:
	FKey ResolveDisplayKey() const;
	void BindActiveSkillTag();
	void UnbindActiveSkillTag();
	void HandleActiveSkillTagChanged(const FGameplayTag ChangedTag, int32 NewCount);

	TWeakObjectPtr<ABasePlayer> CachedPlayer;
	TWeakObjectPtr<UAbilitySystemComponent> BoundAbilitySystemComponent;
	FGameplayTag BoundActiveSkillTag;
	FDelegateHandle ActiveSkillTagEventHandle;
};
