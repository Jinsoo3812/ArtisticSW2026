#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Upgrade/ShipUpgradeTypes.h"
#include "ShipUpgradeDetailsWidget.generated.h"

struct FStreamableHandle;
class UButton;
class UImage;
class UShipUpgradeComponent;
class UShipUpgradeMaterialRowWidget;
class UShipUpgradeStatChangeRowWidget;
class UTextBlock;
class UThrobber;
class UVerticalBox;
class UWidget;

DECLARE_MULTICAST_DELEGATE_OneParam(FShipUpgradeActivationRequestedNative, FName);
DECLARE_MULTICAST_DELEGATE_OneParam(FShipUpgradePreviewRequestedNative, const FShipUpgradeNodeView&);

UCLASS(Abstract, BlueprintType)
class SHIPUPGRADEUI_API UShipUpgradeDetailsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	void SetUpgradeComponent(UShipUpgradeComponent* InComponent);
	void ShowNode(const FShipUpgradeNodeView& InView, bool bRequestPending);
	void ClearNode();
	void SetRequestPending(bool bPending);

	FShipUpgradeActivationRequestedNative& OnActivationRequested() { return ActivationRequestedDelegate; }
	FShipUpgradePreviewRequestedNative& OnPreviewRequested() { return PreviewRequestedDelegate; }

protected:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_NodeName;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Description;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_2DPreview;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> Panel_3DPreview;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> VerticalBox_StatChanges;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> VerticalBox_MaterialCosts;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_UnavailableReason;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Activate;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Activate;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UThrobber> Throbber_Requesting;

	/** Optional network-request indicator. Disabled by default for the compact popup design. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Style")
	bool bShowRequestThrobber = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Rows")
	TSubclassOf<UShipUpgradeStatChangeRowWidget> StatChangeRowClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Rows")
	TSubclassOf<UShipUpgradeMaterialRowWidget> MaterialRowClass;

	UFUNCTION(BlueprintImplementableEvent, Category = "Ship Upgrade|Style")
	void BP_OnDetailsStateApplied(EShipUpgradeNodeState State, bool bCanActivate, bool bRequestPending);

private:
	UFUNCTION()
	void HandleActivateClicked();

	void RebuildStatChanges();
	void RebuildMaterialCosts();
	void RequestPreviewIconLoad();
	void RefreshActivationState();

	UPROPERTY(Transient)
	TObjectPtr<UShipUpgradeComponent> UpgradeComponent;

	UPROPERTY(Transient)
	FShipUpgradeNodeView SelectedView;

	bool bHasSelection = false;
	bool bIsRequestPending = false;
	FSoftObjectPath PendingIconPath;
	TSharedPtr<FStreamableHandle> IconLoadHandle;
	FShipUpgradeActivationRequestedNative ActivationRequestedDelegate;
	FShipUpgradePreviewRequestedNative PreviewRequestedDelegate;
};
