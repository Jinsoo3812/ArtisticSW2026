#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Upgrade/ShipUpgradeTypes.h"
#include "ShipUpgradeNodeWidget.generated.h"

class UBorder;
class UButton;
class UImage;
class UTextBlock;
struct FStreamableHandle;

DECLARE_MULTICAST_DELEGATE_OneParam(FShipUpgradeNodeSelectedNative, FName);
DECLARE_MULTICAST_DELEGATE_TwoParams(FShipUpgradeNodeHoverChangedNative, FName, bool);

/**
 * Native behavior for one ship-upgrade graph node.
 * The Widget Blueprint child only supplies named designer widgets and styling.
 */
UCLASS(Abstract, BlueprintType)
class SHIPUPGRADEUI_API UShipUpgradeNodeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	void ApplyNodeView(const FShipUpgradeNodeView& InView);
	void SetSelected(bool bInSelected);

	FName GetNodeId() const { return NodeView.NodeId; }
	const FShipUpgradeNodeView& GetNodeView() const { return NodeView; }
	FShipUpgradeNodeSelectedNative& OnNodeSelected() { return NodeSelectedDelegate; }
	FShipUpgradeNodeHoverChangedNative& OnNodeHoverChanged() { return NodeHoverChangedDelegate; }

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Node;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_Icon;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_Lock;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_Check;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Name;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> Border_StateGlow;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Style")
	FLinearColor LockedGlowColor = FLinearColor(0.2f, 0.2f, 0.2f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Style")
	FLinearColor InsufficientMaterialsGlowColor = FLinearColor(0.7f, 0.08f, 0.08f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Style")
	FLinearColor AvailableGlowColor = FLinearColor(0.08f, 0.6f, 0.18f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Style")
	FLinearColor ActiveGlowColor = FLinearColor(0.05f, 0.35f, 0.9f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Style", meta = (ClampMin = "0.1"))
	float HoverScale = 1.08f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Style", meta = (ClampMin = "0.1"))
	float SelectedScale = 1.05f;

	UFUNCTION(BlueprintImplementableEvent, Category = "Ship Upgrade|Style")
	void BP_OnVisualStateApplied(EShipUpgradeNodeState State, bool bIsSelected);

	UFUNCTION(BlueprintImplementableEvent, Category = "Ship Upgrade|Style")
	void BP_OnHoverChanged(bool bIsHovered);

private:
	UFUNCTION()
	void HandleClicked();

	UFUNCTION()
	void HandleHovered();

	UFUNCTION()
	void HandleUnhovered();

	void RefreshBuiltInVisuals();
	void RefreshScale(bool bHovered);
	void RequestIconLoad();

	UPROPERTY(Transient)
	FShipUpgradeNodeView NodeView;

	bool bSelected = false;
	FSoftObjectPath PendingIconPath;
	TSharedPtr<FStreamableHandle> IconLoadHandle;
	FShipUpgradeNodeSelectedNative NodeSelectedDelegate;
	FShipUpgradeNodeHoverChangedNative NodeHoverChangedDelegate;
};
