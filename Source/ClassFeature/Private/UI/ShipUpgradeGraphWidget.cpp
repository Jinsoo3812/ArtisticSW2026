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
		return;
	}

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

	if (ConnectionWidgetClass)
	{
		for (const FShipUpgradeNodeView& ChildView : NodeViews)
		{
			const FVector2D ChildCenter = ChildView.GraphPosition + GraphOriginOffset + NodeWidgetSize * 0.5f;
			for (FName ParentId : ChildView.PrerequisiteNodeIds)
			{
				const FShipUpgradeNodeView* ParentView = FindView(ParentId);
				if (!ParentView)
				{
					continue;
				}

				const FVector2D ParentCenter = ParentView->GraphPosition + GraphOriginOffset + NodeWidgetSize * 0.5f;
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
			}
		}
	}

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
				CanvasSlot->SetPosition(View.GraphPosition + GraphOriginOffset);
				CanvasSlot->SetSize(NodeWidgetSize);
				CanvasSlot->SetZOrder(10);
			}

			NodeWidget->ApplyNodeView(View);
			NodeWidget->OnNodeSelected().AddUObject(this, &UShipUpgradeGraphWidget::HandleNodeSelected);
			NodeWidgets.Add(View.NodeId, NodeWidget);
		}
	}
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
