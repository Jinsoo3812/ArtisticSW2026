#include "UI/ShipUpgradeScreenWidget.h"

#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"
#include "UI/ShipUpgradeDetailsWidget.h"
#include "UI/ShipUpgradeGraphWidget.h"
#include "UI/ShipUpgradePreviewStage.h"
#include "Upgrade/ShipUpgradeBlueprintLibrary.h"
#include "Upgrade/ShipUpgradeComponent.h"
#include "Upgrade/ShipUpgradeTreeDataAsset.h"

void UShipUpgradeScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] Screen constructed. Screen=%s OwningPlayer=%s Graph=%s Details=%s GraphExtent=%s"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwningPlayer()),
		*GetNameSafe(GraphWidget),
		*GetNameSafe(DetailsWidget),
		*GetNameSafe(SizeBox_GraphExtent));

	if (GraphWidget)
	{
		GraphWidget->OnNodeSelected().RemoveAll(this);
		GraphWidget->OnNodeSelected().AddUObject(this, &UShipUpgradeScreenWidget::HandleNodeSelected);
	}
	if (DetailsWidget)
	{
		DetailsWidget->OnActivationRequested().RemoveAll(this);
		DetailsWidget->OnPreviewRequested().RemoveAll(this);
		DetailsWidget->OnActivationRequested().AddUObject(this, &UShipUpgradeScreenWidget::HandleActivationRequested);
		DetailsWidget->OnPreviewRequested().AddUObject(this, &UShipUpgradeScreenWidget::HandlePreviewRequested);
	}

	ApplyZoom(1.0f);
	SpawnPreviewStage();
	TryInitialize();
}

void UShipUpgradeScreenWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(InitializationRetryTimer);
	}
	UnbindUpgradeComponent();

	if (GraphWidget)
	{
		GraphWidget->OnNodeSelected().RemoveAll(this);
	}
	if (DetailsWidget)
	{
		DetailsWidget->OnActivationRequested().RemoveAll(this);
		DetailsWidget->OnPreviewRequested().RemoveAll(this);
	}
	if (IsValid(PreviewStage))
	{
		PreviewStage->Destroy();
	}
	PreviewStage = nullptr;

	Super::NativeDestruct();
}

FReply UShipUpgradeScreenWidget::NativeOnMouseButtonDown(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton
		&& IsPointerOverWidget(Image_MainShipPreview, InMouseEvent))
	{
		bDraggingPreview = true;
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UShipUpgradeScreenWidget::NativeOnMouseButtonUp(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	if (bDraggingPreview && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDraggingPreview = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply UShipUpgradeScreenWidget::NativeOnMouseMove(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	if (bDraggingPreview)
	{
		if (PreviewStage)
		{
			PreviewStage->AddPreviewYaw(InMouseEvent.GetCursorDelta().X * PreviewRotationSensitivity);
		}
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UShipUpgradeScreenWidget::NativeOnMouseWheel(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	if (IsPointerOverWidget(GraphWidget, InMouseEvent))
	{
		ApplyZoom(Zoom + InMouseEvent.GetWheelDelta() * ZoomStep);
		return FReply::Handled();
	}
	return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}

void UShipUpgradeScreenWidget::RefreshAll()
{
	if (!UpgradeComponent)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ShipUpgradeUI] RefreshAll skipped: UpgradeComponent is null. Screen=%s"),
			*GetNameSafe(this));
		return;
	}

	CachedNodeViews = UpgradeComponent->GetAllNodeViews();
	UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] Runtime data received. Component=%s Owner=%s UpgradeTree=%s NodeViews=%d"),
		*GetNameSafe(UpgradeComponent),
		*GetNameSafe(UpgradeComponent->GetOwner()),
		*GetNameSafe(UpgradeComponent->UpgradeTree.Get()),
		CachedNodeViews.Num());
	if (SelectedNodeId.IsNone() && !CachedNodeViews.IsEmpty())
	{
		const FShipUpgradeNodeView* PreferredView = CachedNodeViews.FindByPredicate(
			[](const FShipUpgradeNodeView& View)
			{
				return View.State == EShipUpgradeNodeState::Active;
			});
		SelectedNodeId = PreferredView ? PreferredView->NodeId : CachedNodeViews[0].NodeId;
	}

	if (GraphWidget)
	{
		GraphWidget->RebuildGraph(CachedNodeViews);
		GraphWidget->SetSelectedNode(SelectedNodeId);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ShipUpgradeUI] FAILED: GraphWidget is not bound on WBP_ShipUpgradeScreen."));
	}
	UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] Screen refresh continuing. SelectedNode=%s DetailsWidget=%s"),
		*SelectedNodeId.ToString(),
		*GetNameSafe(DetailsWidget));
	RefreshActiveVisuals();
	RefreshSelectedNode();
	RefreshCurrentStats(UpgradeComponent->GetCurrentShipStats());
}

void UShipUpgradeScreenWidget::HandleUpgradeDataReady()
{
	RefreshAll();
}

void UShipUpgradeScreenWidget::HandleUpgradeDataChanged()
{
	RefreshAll();
}

void UShipUpgradeScreenWidget::HandleActivationResult(
	FName NodeId,
	EShipUpgradeActivationResult Result,
	FText Message)
{
	PendingNodeIds.Remove(NodeId);
	if (Text_ResultMessage)
	{
		Text_ResultMessage->SetText(Message);
	}
	if (DetailsWidget && SelectedNodeId == NodeId)
	{
		DetailsWidget->SetRequestPending(false);
	}
	BP_OnActivationResult(NodeId, Result, Message);
	RefreshAll();
}

void UShipUpgradeScreenWidget::HandleShipStatsChanged(FShipStatSnapshot NewStats)
{
	RefreshCurrentStats(NewStats);
}

void UShipUpgradeScreenWidget::TryInitialize()
{
	if (UShipUpgradeComponent* FoundComponent =
		UShipUpgradeBlueprintLibrary::GetLocalShipUpgradeComponent(this))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[ShipUpgradeUI] SUCCESS: Local ShipUpgradeComponent found. Attempt=%d Component=%s Owner=%s Tree=%s"),
			InitializationRetryCount + 1,
			*GetNameSafe(FoundComponent),
			*GetNameSafe(FoundComponent->GetOwner()),
			*GetNameSafe(FoundComponent->UpgradeTree.Get()));
		BindUpgradeComponent(FoundComponent);
		FoundComponent->RefreshUpgradeData();
		RefreshAll();
		return;
	}

	++InitializationRetryCount;
	if (InitializationRetryCount >= MaxInitializationRetries)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ShipUpgradeUI] FAILED: Local ShipUpgradeComponent not found after %d attempts. Check GameMode PlayerStateClass."),
			InitializationRetryCount);
		BP_OnInitializationFailed();
		return;
	}
	if (InitializationRetryCount == 1)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ShipUpgradeUI] Local ShipUpgradeComponent not ready; retrying up to %d times."),
			MaxInitializationRetries);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			InitializationRetryTimer,
			this,
			&UShipUpgradeScreenWidget::TryInitialize,
			InitializationRetryInterval,
			false);
	}
}

void UShipUpgradeScreenWidget::BindUpgradeComponent(UShipUpgradeComponent* InComponent)
{
	if (UpgradeComponent == InComponent)
	{
		return;
	}

	UnbindUpgradeComponent();
	UpgradeComponent = InComponent;
	if (!UpgradeComponent)
	{
		return;
	}

	UpgradeComponent->OnUpgradeDataReady.AddDynamic(this, &UShipUpgradeScreenWidget::HandleUpgradeDataReady);
	UpgradeComponent->OnUpgradeDataChanged.AddDynamic(this, &UShipUpgradeScreenWidget::HandleUpgradeDataChanged);
	UpgradeComponent->OnNodeActivationResult.AddDynamic(this, &UShipUpgradeScreenWidget::HandleActivationResult);
	UpgradeComponent->OnShipStatsChanged.AddDynamic(this, &UShipUpgradeScreenWidget::HandleShipStatsChanged);
	UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] Component events bound. Component=%s Graph=%s Details=%s"),
		*GetNameSafe(UpgradeComponent),
		*GetNameSafe(GraphWidget),
		*GetNameSafe(DetailsWidget));
	if (DetailsWidget)
	{
		DetailsWidget->SetUpgradeComponent(UpgradeComponent);
	}
}

void UShipUpgradeScreenWidget::UnbindUpgradeComponent()
{
	if (!UpgradeComponent)
	{
		return;
	}
	UpgradeComponent->OnUpgradeDataReady.RemoveDynamic(this, &UShipUpgradeScreenWidget::HandleUpgradeDataReady);
	UpgradeComponent->OnUpgradeDataChanged.RemoveDynamic(this, &UShipUpgradeScreenWidget::HandleUpgradeDataChanged);
	UpgradeComponent->OnNodeActivationResult.RemoveDynamic(this, &UShipUpgradeScreenWidget::HandleActivationResult);
	UpgradeComponent->OnShipStatsChanged.RemoveDynamic(this, &UShipUpgradeScreenWidget::HandleShipStatsChanged);
	UpgradeComponent = nullptr;
}

void UShipUpgradeScreenWidget::HandleNodeSelected(FName NodeId)
{
	UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] Node selected. NodeId=%s"),
		*NodeId.ToString());
	SelectedNodeId = NodeId;
	RefreshSelectedNode();
}

void UShipUpgradeScreenWidget::HandleActivationRequested(FName NodeId)
{
	if (!UpgradeComponent || PendingNodeIds.Contains(NodeId))
	{
		return;
	}
	PendingNodeIds.Add(NodeId);
	if (DetailsWidget && SelectedNodeId == NodeId)
	{
		DetailsWidget->SetRequestPending(true);
	}
	UpgradeComponent->RequestActivateNode(NodeId);
}

void UShipUpgradeScreenWidget::HandlePreviewRequested(const FShipUpgradeNodeView& View)
{
	if (!View.PreviewActorClass.IsNull())
	{
		ApplyPreviewClass(View.PreviewActorClass);
	}
	else if (!ActiveShipVisualClass.IsNull())
	{
		ApplyPreviewClass(ActiveShipVisualClass);
	}
	else
	{
		ApplyPreviewClass(DefaultShipPreviewActorClass);
	}
}

void UShipUpgradeScreenWidget::RefreshSelectedNode()
{
	if (!DetailsWidget)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ShipUpgradeUI] FAILED: DetailsWidget is not bound; selected node details cannot render."));
		return;
	}

	const FShipUpgradeNodeView* View = FindCachedView(SelectedNodeId);
	if (!View)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ShipUpgradeUI] Selected node view not found. NodeId=%s"),
			*SelectedNodeId.ToString());
		DetailsWidget->ClearNode();
		return;
	}
	UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] Sending node data to details. NodeId=%s StatRows=%d MaterialRows=%d HasMaterials=%s"),
		*View->NodeId.ToString(),
		View->StatChanges.Num(),
		View->MaterialCosts.Num(),
		View->bHasEnoughMaterials ? TEXT("YES") : TEXT("NO"));
	DetailsWidget->ShowNode(*View, PendingNodeIds.Contains(View->NodeId));
}

void UShipUpgradeScreenWidget::RefreshCurrentStats(const FShipStatSnapshot& Stats)
{
	UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] Current stats received. Health=%.2f Damage=%.2f Cooldown=%.2f ProjectileSpeed=%.2f Propulsion=%.2f Turn=%.2f"),
		Stats.MaxHealth,
		Stats.CannonDamage,
		Stats.CannonFireCooldownSeconds,
		Stats.CannonballSpeed,
		Stats.ForwardPropulsionMultiplier,
		Stats.TurnTorqueMultiplier);
	if (Text_CurrentHealth)
	{
		Text_CurrentHealth->SetText(FText::AsNumber(Stats.MaxHealth));
	}
	if (Text_CurrentCannonDamage)
	{
		Text_CurrentCannonDamage->SetText(FText::AsNumber(Stats.CannonDamage));
	}
	if (Text_CurrentCooldown)
	{
		Text_CurrentCooldown->SetText(FText::Format(
			NSLOCTEXT("ShipUpgrade", "CurrentCooldownFormat", "{0}초"),
			FText::AsNumber(Stats.CannonFireCooldownSeconds)));
	}
	if (Text_CurrentCannonballSpeed)
	{
		Text_CurrentCannonballSpeed->SetText(FText::Format(
			NSLOCTEXT("ShipUpgrade", "CurrentProjectileSpeedFormat", "{0} cm/s"),
			FText::AsNumber(Stats.CannonballSpeed)));
	}
	if (Text_CurrentPropulsion)
	{
		Text_CurrentPropulsion->SetText(FText::AsPercent(Stats.ForwardPropulsionMultiplier));
	}
	if (Text_CurrentTurn)
	{
		Text_CurrentTurn->SetText(FText::AsPercent(Stats.TurnTorqueMultiplier));
	}
	BP_OnCurrentStatsChanged(Stats);
}

void UShipUpgradeScreenWidget::RefreshActiveVisuals()
{
	int32 BestShipPriority = MIN_int32;
	int32 BestCannonPriority = MIN_int32;
	ActiveShipVisualClass.Reset();
	ActiveCannonVisualClass.Reset();

	for (const FShipUpgradeNodeView& View : CachedNodeViews)
	{
		if (View.State != EShipUpgradeNodeState::Active)
		{
			continue;
		}
		if (!View.ActivatedShipActorClass.IsNull() && View.VisualPriority > BestShipPriority)
		{
			BestShipPriority = View.VisualPriority;
			ActiveShipVisualClass = View.ActivatedShipActorClass;
		}
		if (!View.ActivatedCannonActorClass.IsNull() && View.VisualPriority > BestCannonPriority)
		{
			BestCannonPriority = View.VisualPriority;
			ActiveCannonVisualClass = View.ActivatedCannonActorClass;
		}
	}

	BP_OnActiveVisualsChanged(ActiveShipVisualClass, ActiveCannonVisualClass);
	if (SelectedNodeId.IsNone())
	{
		ApplyPreviewClass(
			!ActiveShipVisualClass.IsNull() ? ActiveShipVisualClass : DefaultShipPreviewActorClass);
	}
}

void UShipUpgradeScreenWidget::SpawnPreviewStage()
{
	if (PreviewStage || !PreviewStageClass || !GetWorld())
	{
		return;
	}

	FActorSpawnParameters Parameters;
	Parameters.Owner = GetOwningPlayer();
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PreviewStage = GetWorld()->SpawnActor<AShipUpgradePreviewStage>(
		PreviewStageClass,
		PreviewStageSpawnTransform,
		Parameters);

	if (PreviewStage && Image_MainShipPreview)
	{
		if (UTextureRenderTarget2D* RenderTarget = PreviewStage->GetRenderTarget())
		{
			FSlateBrush Brush = Image_MainShipPreview->GetBrush();
			Brush.SetResourceObject(RenderTarget);
			Image_MainShipPreview->SetBrush(Brush);
		}
	}
}

void UShipUpgradeScreenWidget::ApplyPreviewClass(TSoftClassPtr<AActor> PreviewClass)
{
	if (PreviewStage)
	{
		PreviewStage->SetPreviewActorSoftClass(PreviewClass);
	}
}

const FShipUpgradeNodeView* UShipUpgradeScreenWidget::FindCachedView(FName NodeId) const
{
	return CachedNodeViews.FindByPredicate([NodeId](const FShipUpgradeNodeView& View)
	{
		return View.NodeId == NodeId;
	});
}

bool UShipUpgradeScreenWidget::IsPointerOverWidget(
	const UWidget* Widget,
	const FPointerEvent& MouseEvent) const
{
	return Widget
		&& Widget->GetVisibility() != ESlateVisibility::Collapsed
		&& Widget->GetVisibility() != ESlateVisibility::Hidden
		&& Widget->GetCachedGeometry().IsUnderLocation(MouseEvent.GetScreenSpacePosition());
}

void UShipUpgradeScreenWidget::ApplyZoom(float NewZoom)
{
	Zoom = FMath::Clamp(NewZoom, ZoomMin, ZoomMax);
	if (GraphWidget)
	{
		GraphWidget->SetRenderScale(FVector2D(Zoom));
		GraphWidget->SetRenderTransformPivot(FVector2D::ZeroVector);
	}
	if (SizeBox_GraphExtent)
	{
		SizeBox_GraphExtent->SetWidthOverride(BaseGraphExtent.X * Zoom);
		SizeBox_GraphExtent->SetHeightOverride(BaseGraphExtent.Y * Zoom);
	}
}
