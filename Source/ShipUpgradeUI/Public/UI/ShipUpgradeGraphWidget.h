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
	FVector2D GetNodeDisplayPosition(FName NodeId) const;
	FVector2D GetRequiredExtent() const;
	UShipUpgradeNodeWidget* GetNodeWidget(FName NodeId) const;
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

	/** Converts the legacy left-to-right data coordinates into a prerequisite-first vertical tree. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph")
	bool bUseVerticalTreeLayout = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph", meta = (ClampMin = "100.0"))
	float VerticalLayerSpacing = 340.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph", meta = (ClampMin = "0.1"))
	float HorizontalBranchScale = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph", meta = (ClampMin = "100.0"))
	float VerticalTreeCenterX = 550.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph", meta = (ClampMin = "0.0"))
	float HorizontalExtentPadding = 120.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph", meta = (ClampMin = "0.0"))
	float ExtentPadding = 300.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph", meta = (ClampMin = "1.0"))
	float ConnectionThickness = 4.0f;

	/** Routes every prerequisite edge through axis-aligned segments instead of one diagonal segment. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship Upgrade|Graph")
	bool bUseOrthogonalConnections = true;

	/** Places the horizontal elbow between the parent exit and child entry. */
	UPROPERTY(
		EditDefaultsOnly,
		BlueprintReadOnly,
		Category = "Ship Upgrade|Graph",
		meta = (ClampMin = "0.1", ClampMax = "0.9"))
	float OrthogonalConnectionElbowRatio = 0.5f;

private:
	void HandleNodeSelected(FName NodeId);
	const FShipUpgradeNodeView* FindView(FName NodeId) const;
	bool AddConnectionSegment(const FVector2D& Start, const FVector2D& End, bool bDashed);
	void BuildDisplayPositions();
	int32 CalculateNodeDepth(FName NodeId, TMap<FName, int32>& DepthCache, TSet<FName>& Visiting) const;

	UPROPERTY(Transient)
	TArray<FShipUpgradeNodeView> NodeViews;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UShipUpgradeNodeWidget>> NodeWidgets;

	TMap<FName, FVector2D> NodeDisplayPositions;
	FShipUpgradeGraphNodeSelectedNative NodeSelectedDelegate;
};
