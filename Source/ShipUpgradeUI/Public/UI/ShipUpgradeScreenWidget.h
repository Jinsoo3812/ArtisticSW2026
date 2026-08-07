#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Upgrade/ShipUpgradeTypes.h"
#include "ShipUpgradeScreenWidget.generated.h"

class AShipUpgradePreviewStage;
class UImage;
class UMaterialInstanceDynamic;
class UScaleBox;
class UShipUpgradeComponent;
class UShipUpgradeDetailsWidget;
class UShipUpgradeGraphWidget;
class USizeBox;
class UTextBlock;

/**
 * Native coordinator for the entire upgrade screen.
 * Data acquisition, delegate binding, graph rebuilding, selection, activation,
 * pending requests, stat text, zoom and preview-stage lifetime live here.
 */
UCLASS(Abstract, BlueprintType)
class SHIPUPGRADEUI_API UShipUpgradeScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UFUNCTION(BlueprintCallable, Category = "Ship Upgrade|UI")
	void RefreshAll();

	UFUNCTION(BlueprintPure, Category = "Ship Upgrade|UI")
	UShipUpgradeComponent* GetUpgradeComponent() const { return UpgradeComponent; }

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UShipUpgradeGraphWidget> GraphWidget;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UShipUpgradeDetailsWidget> DetailsWidget;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> SizeBox_GraphExtent;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_MainShipPreview;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_CurrentHealth;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_CurrentCannonDamage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_CurrentCooldown;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_CurrentCannonballSpeed;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_CurrentPropulsion;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_CurrentTurn;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_ResultMessage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph")
	FVector2D BaseGraphExtent = FVector2D(2400.0f, 1600.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph", meta = (ClampMin = "100.0"))
	float VerticalGraphMinWidth = 1100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph", meta = (ClampMin = "100.0"))
	float VerticalGraphMinHeight = 1600.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Details")
	FVector2D DetailsNodeOffset = FVector2D(24.0f, -20.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Details", meta = (ClampMin = "200.0"))
	float DetailsPopupWidth = 360.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Details", meta = (ClampMin = "100.0"))
	float DetailsPopupMaxHeight = 420.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph", meta = (ClampMin = "0.1"))
	float ZoomMin = 0.6f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph", meta = (ClampMin = "0.1"))
	float ZoomMax = 1.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph", meta = (ClampMin = "0.01"))
	float ZoomStep = 0.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Initialization", meta = (ClampMin = "1"))
	int32 MaxInitializationRetries = 30;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Initialization", meta = (ClampMin = "0.01"))
	float InitializationRetryInterval = 0.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Preview")
	TSubclassOf<AShipUpgradePreviewStage> PreviewStageClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Preview")
	FTransform PreviewStageSpawnTransform = FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, -100000.0f));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Preview")
	TSoftClassPtr<AActor> DefaultShipPreviewActorClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Preview", meta = (ClampMin = "0.01"))
	float PreviewRotationSensitivity = 0.25f;

	UFUNCTION(BlueprintImplementableEvent, Category = "Ship Upgrade|UI")
	void BP_OnActivationResult(FName NodeId, EShipUpgradeActivationResult Result, const FText& Message);

	UFUNCTION(BlueprintImplementableEvent, Category = "Ship Upgrade|UI")
	void BP_OnCurrentStatsChanged(const FShipStatSnapshot& CurrentStats);

	UFUNCTION(BlueprintImplementableEvent, Category = "Ship Upgrade|Preview")
	void BP_OnActiveVisualsChanged(
		const TSoftClassPtr<AActor>& ActiveShipActorClass,
		const TSoftClassPtr<AActor>& ActiveCannonActorClass);

	UFUNCTION(BlueprintImplementableEvent, Category = "Ship Upgrade|UI")
	void BP_OnInitializationFailed();

private:
	UFUNCTION()
	void HandleUpgradeDataReady();

	UFUNCTION()
	void HandleUpgradeDataChanged();

	UFUNCTION()
	void HandleActivationResult(FName NodeId, EShipUpgradeActivationResult Result, FText Message);

	UFUNCTION()
	void HandleShipStatsChanged(FShipStatSnapshot NewStats);

	void TryInitialize();
	void BindUpgradeComponent(UShipUpgradeComponent* InComponent);
	void UnbindUpgradeComponent();
	void HandleNodeSelected(FName NodeId);
	void HandleActivationRequested(FName NodeId);
	void HandlePreviewRequested(const FShipUpgradeNodeView& View);
	void RefreshSelectedNode();
	void RefreshCurrentStats(const FShipStatSnapshot& Stats);
	void RefreshActiveVisuals();
	void SetupPreviewLayers();
	void SetupDetailsPopup();
	void SetDetailsPopupVisible(bool bVisible);
	void SpawnPreviewStage();
	void ApplyPreviewClass(TSoftClassPtr<AActor> PreviewClass);
	void ApplyCurrentShipPreview();
	void PositionDetailsNextToNode(FName NodeId);
	void UpdateGraphExtent();
	const FShipUpgradeNodeView* FindCachedView(FName NodeId) const;
	bool IsPointerOverWidget(const UWidget* Widget, const FPointerEvent& MouseEvent) const;
	void ApplyZoom(float NewZoom);

	UPROPERTY(Transient)
	TObjectPtr<UShipUpgradeComponent> UpgradeComponent;

	UPROPERTY(Transient)
	TArray<FShipUpgradeNodeView> CachedNodeViews;

	UPROPERTY(Transient)
	TObjectPtr<AShipUpgradePreviewStage> PreviewStage;

	UPROPERTY(Transient)
	TObjectPtr<UImage> Image_ShipModelOverlay;

	UPROPERTY(Transient)
	TObjectPtr<UScaleBox> PreviewModelScaleBox;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> DetailsPopupHost;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewOverlayMaterialInstance;

	UPROPERTY(Transient)
	TSet<FName> PendingNodeIds;

	FName SelectedNodeId;
	TSoftClassPtr<AActor> ActiveShipVisualClass;
	TSoftClassPtr<AActor> ActiveCannonVisualClass;
	float Zoom = 1.0f;
	int32 InitializationRetryCount = 0;
	bool bDraggingPreview = false;
	FTimerHandle InitializationRetryTimer;
};
