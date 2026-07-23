#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Upgrade/ShipUpgradeTypes.h"
#include "ShipUpgradeGraphWidget.generated.h"

class UCanvasPanel;
class UShipUpgradeConnectionWidget;
class UShipUpgradeNodeWidget;

DECLARE_MULTICAST_DELEGATE_OneParam(FShipUpgradeGraphNodeSelectedNative, FName);

/**
 * Creates all node and connection widgets from FShipUpgradeNodeView.
 * The Widget Blueprint child only needs a CanvasPanel named CanvasPanel_Graph
 * and class assignments for the node/connection visual children.
 */
UCLASS(Abstract, BlueprintType)
class SHIPUPGRADEUI_API UShipUpgradeGraphWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeDestruct() override;

	void RebuildGraph(const TArray<FShipUpgradeNodeView>& InViews);
	void SetSelectedNode(FName NodeId);
	FShipUpgradeGraphNodeSelectedNative& OnNodeSelected() { return NodeSelectedDelegate; }

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> CanvasPanel_Graph;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph")
	TSubclassOf<UShipUpgradeNodeWidget> NodeWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph")
	TSubclassOf<UShipUpgradeConnectionWidget> ConnectionWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph")
	FVector2D NodeWidgetSize = FVector2D(160.0f, 130.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph")
	FVector2D GraphOriginOffset = FVector2D(300.0f, 150.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph", meta = (ClampMin = "1.0"))
	float ConnectionThickness = 4.0f;

private:
	void HandleNodeSelected(FName NodeId);
	const FShipUpgradeNodeView* FindView(FName NodeId) const;

	UPROPERTY(Transient)
	TArray<FShipUpgradeNodeView> NodeViews;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UShipUpgradeNodeWidget>> NodeWidgets;

	FShipUpgradeGraphNodeSelectedNative NodeSelectedDelegate;
};
