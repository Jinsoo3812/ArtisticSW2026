#include "UI/ShipUpgradeGraphWidget.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "UI/ShipUpgradeConnectionWidget.h"
#include "UI/ShipUpgradeNodeWidget.h"

void UShipUpgradeGraphWidget::NativeDestruct()
{
	for (const TPair<FName, TObjectPtr<UShipUpgradeNodeWidget>>& Pair : NodeWidgets)
	{
		if (Pair.Value)
		{
			Pair.Value->OnNodeSelected().RemoveAll(this);
		}
	}
	NodeWidgets.Reset();
	NodeSelectedDelegate.Clear();

	Super::NativeDestruct();
}

void UShipUpgradeGraphWidget::RebuildGraph(const TArray<FShipUpgradeNodeView>& InViews)
{
	if (!CanvasPanel_Graph)
	{
		/* UE_LOG(LogTemp, Error,
			TEXT("[ShipUpgradeUI] FAILED: CanvasPanel_Graph is not bound. Graph=%s"),
			*GetNameSafe(this)); */
		return;
	}

	/* UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] RebuildGraph started. Graph=%s Views=%d NodeClass=%s ConnectionClass=%s CanvasSize=%s"),
		*GetNameSafe(this),
		InViews.Num(),
		*GetNameSafe(NodeWidgetClass.Get()),
		*GetNameSafe(ConnectionWidgetClass.Get()),
		*CanvasPanel_Graph->GetCachedGeometry().GetLocalSize().ToString()); */

	for (const TPair<FName, TObjectPtr<UShipUpgradeNodeWidget>>& Pair : NodeWidgets)
	{
		if (Pair.Value)
		{
			Pair.Value->OnNodeSelected().RemoveAll(this);
		}
	}

	CanvasPanel_Graph->ClearChildren();
	NodeViews = InViews;
	NodeWidgets.Reset();
	BuildDisplayPositions();

	int32 CreatedConnectionCount = 0;
	if (ConnectionWidgetClass)
	{
		for (const FShipUpgradeNodeView& ChildView : NodeViews)
		{
			const FVector2D ChildCenter = GetNodeDisplayPosition(ChildView.NodeId) + NodeWidgetSize * 0.5f;
			for (FName ParentId : ChildView.PrerequisiteNodeIds)
			{
				const FShipUpgradeNodeView* ParentView = FindView(ParentId);
				if (!ParentView)
				{
					continue;
				}

				const FVector2D ParentCenter = GetNodeDisplayPosition(ParentId) + NodeWidgetSize * 0.5f;
				const FVector2D Delta = ChildCenter - ParentCenter;
				const float Length = Delta.Size();
				if (Length <= KINDA_SMALL_NUMBER)
				{
					continue;
				}

				UShipUpgradeConnectionWidget* Connection = CreateWidget<UShipUpgradeConnectionWidget>(
					GetOwningPlayer(), ConnectionWidgetClass);
				if (!Connection)
				{
					continue;
				}

				CanvasPanel_Graph->AddChild(Connection);
				if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Connection->Slot))
				{
					CanvasSlot->SetPosition(ParentCenter);
					CanvasSlot->SetSize(FVector2D(Length, ConnectionThickness));
					CanvasSlot->SetAlignment(FVector2D(0.0f, 0.5f));
					CanvasSlot->SetZOrder(0);
				}

				const float AngleDegrees = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
				Connection->SetRenderTransformPivot(FVector2D(0.0f, 0.5f));
				Connection->SetRenderTransformAngle(AngleDegrees);
				Connection->ConfigureConnection(ChildView.State == EShipUpgradeNodeState::Locked);
				++CreatedConnectionCount;
			}
		}
	}
	else
	{
		/* UE_LOG(LogTemp, Warning,
			TEXT("[ShipUpgradeUI] ConnectionWidgetClass is None; nodes can appear but connection lines will not.")); */
	}

	int32 CreatedNodeCount = 0;
	if (NodeWidgetClass)
	{
		for (const FShipUpgradeNodeView& View : NodeViews)
		{
			UShipUpgradeNodeWidget* NodeWidget = CreateWidget<UShipUpgradeNodeWidget>(GetOwningPlayer(), NodeWidgetClass);
			if (!NodeWidget)
			{
				continue;
			}

			CanvasPanel_Graph->AddChild(NodeWidget);
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(NodeWidget->Slot))
			{
				CanvasSlot->SetPosition(GetNodeDisplayPosition(View.NodeId));
				CanvasSlot->SetSize(NodeWidgetSize);
				CanvasSlot->SetZOrder(10);
			}

			NodeWidget->ApplyNodeView(View);
			NodeWidget->OnNodeSelected().AddUObject(this, &UShipUpgradeGraphWidget::HandleNodeSelected);
			NodeWidgets.Add(View.NodeId, NodeWidget);
			++CreatedNodeCount;
			/* UE_LOG(LogTemp, Log,
				TEXT("[ShipUpgradeUI] Node widget created. NodeId=%s State=%s DataPosition=%s RuntimePosition=%s Widget=%s"),
				*View.NodeId.ToString(),
				*UEnum::GetValueAsString(View.State),
				*View.GraphPosition.ToString(),
				*(View.GraphPosition + GraphOriginOffset).ToString(),
				*GetNameSafe(NodeWidget)); */
		}
	}
	else
	{
		/* UE_LOG(LogTemp, Error,
			TEXT("[ShipUpgradeUI] FAILED: NodeWidgetClass is None; no node widgets can be created.")); */
	}

	/* UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] RebuildGraph finished. RequestedNodes=%d CreatedNodes=%d CreatedConnections=%d CanvasChildren=%d"),
		InViews.Num(),
		CreatedNodeCount,
		CreatedConnectionCount,
		CanvasPanel_Graph->GetChildrenCount()); */
}

FVector2D UShipUpgradeGraphWidget::GetNodeDisplayPosition(FName NodeId) const
{
	if (const FVector2D* Position = NodeDisplayPositions.Find(NodeId))
	{
		return *Position;
	}
	if (const FShipUpgradeNodeView* View = FindView(NodeId))
	{
		return View->GraphPosition + GraphOriginOffset;
	}
	return GraphOriginOffset;
}

FVector2D UShipUpgradeGraphWidget::GetRequiredExtent() const
{
	FVector2D Extent = GraphOriginOffset + NodeWidgetSize;
	for (const TPair<FName, FVector2D>& Pair : NodeDisplayPositions)
	{
		Extent.X = FMath::Max(Extent.X, Pair.Value.X + NodeWidgetSize.X);
		Extent.Y = FMath::Max(Extent.Y, Pair.Value.Y + NodeWidgetSize.Y);
	}
	return Extent + FVector2D(HorizontalExtentPadding, ExtentPadding);
}

UShipUpgradeNodeWidget* UShipUpgradeGraphWidget::GetNodeWidget(FName NodeId) const
{
	if (const TObjectPtr<UShipUpgradeNodeWidget>* Widget = NodeWidgets.Find(NodeId))
	{
		return Widget->Get();
	}
	return nullptr;
}

void UShipUpgradeGraphWidget::SetSelectedNode(FName NodeId)
{
	for (const TPair<FName, TObjectPtr<UShipUpgradeNodeWidget>>& Pair : NodeWidgets)
	{
		if (Pair.Value)
		{
			Pair.Value->SetSelected(Pair.Key == NodeId);
		}
	}
}

void UShipUpgradeGraphWidget::HandleNodeSelected(FName NodeId)
{
	SetSelectedNode(NodeId);
	NodeSelectedDelegate.Broadcast(NodeId);
}

const FShipUpgradeNodeView* UShipUpgradeGraphWidget::FindView(FName NodeId) const
{
	return NodeViews.FindByPredicate([NodeId](const FShipUpgradeNodeView& View)
	{
		return View.NodeId == NodeId;
	});
}

void UShipUpgradeGraphWidget::BuildDisplayPositions()
{
	NodeDisplayPositions.Reset();
	if (!bUseVerticalTreeLayout)
	{
		for (const FShipUpgradeNodeView& View : NodeViews)
		{
			NodeDisplayPositions.Add(View.NodeId, View.GraphPosition + GraphOriginOffset);
		}
		return;
	}

	TMap<FName, int32> DepthCache;
	TSet<FName> Visiting;
	for (const FShipUpgradeNodeView& View : NodeViews)
	{
		const int32 Depth = CalculateNodeDepth(View.NodeId, DepthCache, Visiting);
		NodeDisplayPositions.Add(
			View.NodeId,
			FVector2D(
				VerticalTreeCenterX - NodeWidgetSize.X * 0.5f
					+ View.GraphPosition.Y * HorizontalBranchScale,
				GraphOriginOffset.Y + static_cast<float>(Depth) * VerticalLayerSpacing));
	}
}

int32 UShipUpgradeGraphWidget::CalculateNodeDepth(
	FName NodeId,
	TMap<FName, int32>& DepthCache,
	TSet<FName>& Visiting) const
{
	if (const int32* CachedDepth = DepthCache.Find(NodeId))
	{
		return *CachedDepth;
	}
	if (Visiting.Contains(NodeId))
	{
		return 0;
	}

	Visiting.Add(NodeId);
	int32 Depth = 0;
	if (const FShipUpgradeNodeView* View = FindView(NodeId))
	{
		for (FName ParentId : View->PrerequisiteNodeIds)
		{
			if (FindView(ParentId))
			{
				Depth = FMath::Max(Depth, CalculateNodeDepth(ParentId, DepthCache, Visiting) + 1);
			}
		}
	}
	Visiting.Remove(NodeId);
	DepthCache.Add(NodeId, Depth);
	return Depth;
}
