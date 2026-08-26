#include "UI/ShipUpgradeScreenWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Ship.h"
#include "Framework/Application/SlateApplication.h"
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
	/* UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] Screen constructed. Screen=%s OwningPlayer=%s Graph=%s Details=%s GraphExtent=%s"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwningPlayer()),
		*GetNameSafe(GraphWidget),
		*GetNameSafe(DetailsWidget),
		*GetNameSafe(SizeBox_GraphExtent)); */

	// These helpers reparent designer widgets into runtime hosts. Do that before
	// binding native delegates because releasing a UserWidget's Slate resources
	// during reparenting can run NativeDestruct and clear those delegates.
	SetupPreviewLayers();
	SetupDetailsPopup();

	if (GraphWidget)
	{
		GraphWidget->OnNodeSelected().RemoveAll(this);
		GraphWidget->OnNodeHoverChanged().RemoveAll(this);
		GraphWidget->OnNodeSelected().AddUObject(this, &UShipUpgradeScreenWidget::HandleNodeSelected);
		GraphWidget->OnNodeHoverChanged().AddUObject(this, &UShipUpgradeScreenWidget::HandleNodeHoverChanged);
	}
	if (DetailsWidget)
	{
		DetailsWidget->OnActivationRequested().RemoveAll(this);
		DetailsWidget->OnPreviewRequested().RemoveAll(this);
		DetailsWidget->OnActivationRequested().AddUObject(this, &UShipUpgradeScreenWidget::HandleActivationRequested);
		DetailsWidget->OnPreviewRequested().AddUObject(this, &UShipUpgradeScreenWidget::HandlePreviewRequested);
	}

	SpawnPreviewStage();
	if (DetailsWidget)
	{
		DetailsWidget->ClearNode();
	}
	SetDetailsPopupVisible(false);
	TryInitialize();
}

void UShipUpgradeScreenWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(InitializationRetryTimer);
		World->GetTimerManager().ClearTimer(DetailsTooltipHideTimer);
	}
	UnbindUpgradeComponent();

	if (GraphWidget)
	{
		GraphWidget->OnNodeSelected().RemoveAll(this);
		GraphWidget->OnNodeHoverChanged().RemoveAll(this);
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
	if (bTooltipAwaitingExit && DetailsPopupHost && !DetailsPopupHost->IsHovered())
	{
		HideDetailsTooltip();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UShipUpgradeScreenWidget::NativeOnMouseWheel(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	// Let the parent ScrollBox consume the wheel so the prerequisite tree moves
	// vertically. Zooming a graph under the cursor made the intended scroll UI
	// feel broken and also changed the clickable geometry.
	return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}

void UShipUpgradeScreenWidget::RefreshAll()
{
	if (!UpgradeComponent)
	{
		/* UE_LOG(LogTemp, Warning,
			TEXT("[ShipUpgradeUI] RefreshAll skipped: UpgradeComponent is null. Screen=%s"),
			*GetNameSafe(this)); */
		return;
	}

	CachedNodeViews = UpgradeComponent->GetAllNodeViews();
	/* UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] Runtime data received. Component=%s Owner=%s UpgradeTree=%s NodeViews=%d"),
		*GetNameSafe(UpgradeComponent),
		*GetNameSafe(UpgradeComponent->GetOwner()),
		*GetNameSafe(UpgradeComponent->UpgradeTree.Get()),
		CachedNodeViews.Num()); */
	if (GraphWidget)
	{
		UpdateGraphViewportWidth();
		GraphWidget->RebuildGraph(CachedNodeViews);
		GraphWidget->SetSelectedNode(SelectedNodeId);
		UpdateGraphExtent();
	}
	else
	{
		/* UE_LOG(LogTemp, Error,
			TEXT("[ShipUpgradeUI] FAILED: GraphWidget is not bound on WBP_ShipUpgradeScreen.")); */
	}
	/* UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] Screen refresh continuing. SelectedNode=%s DetailsWidget=%s"),
		*SelectedNodeId.ToString(),
		*GetNameSafe(DetailsWidget)); */
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
		/* UE_LOG(LogTemp, Log,
			TEXT("[ShipUpgradeUI] SUCCESS: Local ShipUpgradeComponent found. Attempt=%d Component=%s Owner=%s Tree=%s"),
			InitializationRetryCount + 1,
			*GetNameSafe(FoundComponent),
			*GetNameSafe(FoundComponent->GetOwner()),
			*GetNameSafe(FoundComponent->UpgradeTree.Get())); */
		BindUpgradeComponent(FoundComponent);
		FoundComponent->RefreshUpgradeData();
		RefreshAll();
		return;
	}

	++InitializationRetryCount;
	if (InitializationRetryCount >= MaxInitializationRetries)
	{
		/* UE_LOG(LogTemp, Error,
			TEXT("[ShipUpgradeUI] FAILED: Local ShipUpgradeComponent not found after %d attempts. Check GameMode PlayerStateClass."),
			InitializationRetryCount); */
		BP_OnInitializationFailed();
		return;
	}
	if (InitializationRetryCount == 1)
	{
		/* UE_LOG(LogTemp, Warning,
			TEXT("[ShipUpgradeUI] Local ShipUpgradeComponent not ready; retrying up to %d times."),
			MaxInitializationRetries); */
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
	/* UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] Component events bound. Component=%s Graph=%s Details=%s"),
		*GetNameSafe(UpgradeComponent),
		*GetNameSafe(GraphWidget),
		*GetNameSafe(DetailsWidget)); */
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
	/* UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] Node selected. NodeId=%s"),
		*NodeId.ToString()); */
	SelectedNodeId = NodeId;
	if (GraphWidget)
	{
		GraphWidget->SetSelectedNode(NodeId);
	}
	RefreshSelectedNode();
	PositionDetailsNextToCursor();
}

void UShipUpgradeScreenWidget::HandleNodeHoverChanged(FName NodeId, bool bIsHovered)
{
	if (bIsHovered)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(DetailsTooltipHideTimer);
		}
		bTooltipAwaitingExit = false;
		HoveredNodeId = NodeId;
		SelectedNodeId = NodeId;
		RefreshSelectedNode();
		PositionDetailsNextToCursor();
		return;
	}

	if (HoveredNodeId != NodeId)
	{
		return;
	}
	HoveredNodeId = NAME_None;
	if (UWorld* World = GetWorld())
	{
		if (DetailsTooltipHideDelay > KINDA_SMALL_NUMBER)
		{
			World->GetTimerManager().SetTimer(
				DetailsTooltipHideTimer,
				this,
				&UShipUpgradeScreenWidget::HandleDetailsTooltipHideTimer,
				DetailsTooltipHideDelay,
				false);
			return;
		}
	}
	HandleDetailsTooltipHideTimer();
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
	if (View.PreviewType != EShipUpgradePreviewType::Icon2D && !View.PreviewActorClass.IsNull())
	{
		ApplyPreviewClass(View.PreviewActorClass);
	}
	else if (!ActiveShipVisualClass.IsNull())
	{
		ApplyPreviewClass(ActiveShipVisualClass);
	}
	else
	{
		ApplyCurrentShipPreview();
	}
}

void UShipUpgradeScreenWidget::RefreshSelectedNode()
{
	if (!DetailsWidget)
	{
		/* UE_LOG(LogTemp, Error,
			TEXT("[ShipUpgradeUI] FAILED: DetailsWidget is not bound; selected node details cannot render.")); */
		return;
	}

	const FShipUpgradeNodeView* View = FindCachedView(SelectedNodeId);
	if (!View)
	{
		/* UE_LOG(LogTemp, Warning,
			TEXT("[ShipUpgradeUI] Selected node view not found. NodeId=%s"),
			*SelectedNodeId.ToString()); */
		DetailsWidget->ClearNode();
		SetDetailsPopupVisible(false);
		return;
	}
	/* UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] Sending node data to details. NodeId=%s StatRows=%d MaterialRows=%d HasMaterials=%s"),
		*View->NodeId.ToString(),
		View->StatChanges.Num(),
		View->MaterialCosts.Num(),
		View->bHasEnoughMaterials ? TEXT("YES") : TEXT("NO")); */
	DetailsWidget->ShowNode(*View, PendingNodeIds.Contains(View->NodeId));
	SetDetailsPopupVisible(true);
}

void UShipUpgradeScreenWidget::RefreshCurrentStats(const FShipStatSnapshot& Stats)
{
	/* UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] Current stats received. Health=%.2f Damage=%.2f Cooldown=%.2f ProjectileSpeed=%.2f Propulsion=%.2f Turn=%.2f"),
		Stats.MaxHealth,
		Stats.CannonDamage,
		Stats.CannonFireCooldownSeconds,
		Stats.CannonballSpeed,
		Stats.ForwardPropulsionMultiplier,
		Stats.TurnTorqueMultiplier); */
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
	if (!ActiveShipVisualClass.IsNull())
	{
		ApplyPreviewClass(ActiveShipVisualClass);
	}
	else
	{
		ApplyCurrentShipPreview();
	}
}

void UShipUpgradeScreenWidget::SetupPreviewLayers()
{
	if (!Image_MainShipPreview || Image_ShipModelOverlay || !WidgetTree)
	{
		return;
	}

	UPanelWidget* Parent = Image_MainShipPreview->GetParent();
	if (!Parent)
	{
		return;
	}
	const int32 ChildIndex = Parent->GetChildIndex(Image_MainShipPreview);
	UPanelSlot* OriginalSlot = Image_MainShipPreview->Slot;
	if (ChildIndex == INDEX_NONE || !OriginalSlot)
	{
		return;
	}

	UOverlay* PreviewOverlay = WidgetTree->ConstructWidget<UOverlay>(
		UOverlay::StaticClass(),
		TEXT("Overlay_ShipPreviewLayers"));
	PreviewModelScaleBox = WidgetTree->ConstructWidget<UScaleBox>(
		UScaleBox::StaticClass(),
		TEXT("ScaleBox_ShipModelOverlay"));
	Image_ShipModelOverlay = WidgetTree->ConstructWidget<UImage>(
		UImage::StaticClass(),
		TEXT("Image_ShipModelOverlay"));
	if (!PreviewOverlay || !PreviewModelScaleBox || !Image_ShipModelOverlay)
	{
		return;
	}

	Parent->RemoveChildAt(ChildIndex);
	if (!Parent->InsertChildAt(ChildIndex, PreviewOverlay, OriginalSlot))
	{
		return;
	}

	if (UOverlaySlot* BackgroundSlot = PreviewOverlay->AddChildToOverlay(Image_MainShipPreview))
	{
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	}
	if (UOverlaySlot* ModelSlot = PreviewOverlay->AddChildToOverlay(PreviewModelScaleBox))
	{
		ModelSlot->SetHorizontalAlignment(HAlign_Fill);
		ModelSlot->SetVerticalAlignment(VAlign_Fill);
	}

	PreviewModelScaleBox->SetStretch(EStretch::ScaleToFit);
	PreviewModelScaleBox->SetStretchDirection(EStretchDirection::Both);
	PreviewModelScaleBox->AddChild(Image_ShipModelOverlay);
	Image_ShipModelOverlay->SetVisibility(ESlateVisibility::HitTestInvisible);

	UMaterialInterface* OverlayMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Blueprints/02_UI/UI_WorkTable/UI_ShipUpgrade/M_ShipPreviewOverlay.M_ShipPreviewOverlay"));
	if (OverlayMaterial)
	{
		Image_ShipModelOverlay->SetBrushFromMaterial(OverlayMaterial);
		PreviewOverlayMaterialInstance = Image_ShipModelOverlay->GetDynamicMaterial();
	}
}

void UShipUpgradeScreenWidget::SetupDetailsPopup()
{
	if (!DetailsWidget || DetailsPopupHost || !WidgetTree)
	{
		return;
	}

	UPanelWidget* Parent = DetailsWidget->GetParent();
	if (!Parent)
	{
		return;
	}
	const int32 ChildIndex = Parent->GetChildIndex(DetailsWidget);
	UPanelSlot* OriginalSlot = DetailsWidget->Slot;
	if (ChildIndex == INDEX_NONE || !OriginalSlot)
	{
		return;
	}

	DetailsPopupHost = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(),
		TEXT("SizeBox_ShipUpgradeDetailsPopup"));
	if (!DetailsPopupHost)
	{
		return;
	}

	Parent->RemoveChildAt(ChildIndex);
	UPanelSlot* PopupSlot = Parent->InsertChildAt(ChildIndex, DetailsPopupHost, OriginalSlot);
	if (!PopupSlot)
	{
		DetailsPopupHost = nullptr;
		return;
	}
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(PopupSlot))
	{
		CanvasSlot->SetAnchors(FAnchors(0.0f));
		CanvasSlot->SetAlignment(FVector2D::ZeroVector);
		CanvasSlot->SetPosition(FVector2D::ZeroVector);
		CanvasSlot->SetAutoSize(true);
		CanvasSlot->SetZOrder(100);
	}

	DetailsPopupHost->SetWidthOverride(DetailsPopupWidth);
	DetailsPopupHost->SetMaxDesiredHeight(DetailsPopupMaxHeight);
	DetailsPopupHost->AddChild(DetailsWidget);
	DetailsPopupHost->SetVisibility(ESlateVisibility::Collapsed);
}

void UShipUpgradeScreenWidget::SetDetailsPopupVisible(bool bVisible)
{
	if (DetailsPopupHost)
	{
		DetailsPopupHost->SetVisibility(
			bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
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

	if (PreviewStage && Image_ShipModelOverlay)
	{
		if (UTextureRenderTarget2D* RenderTarget = PreviewStage->GetRenderTarget())
		{
			if (PreviewOverlayMaterialInstance)
			{
				PreviewOverlayMaterialInstance->SetTextureParameterValue(
					TEXT("ShipPreviewTexture"),
					RenderTarget);
			}
			else
			{
				FSlateBrush Brush = Image_ShipModelOverlay->GetBrush();
				Brush.SetResourceObject(RenderTarget);
				Brush.SetImageSize(FVector2D(
					static_cast<float>(RenderTarget->SizeX),
					static_cast<float>(RenderTarget->SizeY)));
				Image_ShipModelOverlay->SetBrush(Brush);
			}
		}
	}
	ApplyCurrentShipPreview();
}

void UShipUpgradeScreenWidget::ApplyPreviewClass(TSoftClassPtr<AActor> PreviewClass)
{
	if (PreviewStage)
	{
		PreviewStage->SetPreviewActorSoftClass(PreviewClass);
	}
}

void UShipUpgradeScreenWidget::ApplyCurrentShipPreview()
{
	if (!PreviewStage || !GetWorld())
	{
		return;
	}

	const APawn* PlayerPawn = GetOwningPlayerPawn();
	AShip* BestShip = nullptr;
	double BestDistanceSquared = TNumericLimits<double>::Max();
	for (TActorIterator<AShip> It(GetWorld()); It; ++It)
	{
		AShip* Candidate = *It;
		if (!IsValid(Candidate) || Candidate->IsEnemyShipForEffects())
		{
			continue;
		}
		const double DistanceSquared = PlayerPawn
			? FVector::DistSquared(PlayerPawn->GetActorLocation(), Candidate->GetActorLocation())
			: 0.0;
		if (!BestShip || DistanceSquared < BestDistanceSquared)
		{
			BestShip = Candidate;
			BestDistanceSquared = DistanceSquared;
		}
	}

	if (BestShip)
	{
		PreviewStage->SetPreviewSourceActor(BestShip);
	}
	else if (!DefaultShipPreviewActorClass.IsNull())
	{
		PreviewStage->SetPreviewActorSoftClass(DefaultShipPreviewActorClass);
	}
}

void UShipUpgradeScreenWidget::PositionDetailsNextToCursor()
{
	if (!DetailsWidget || !DetailsPopupHost)
	{
		return;
	}

	DetailsPopupHost->SetRenderTranslation(FVector2D::ZeroVector);
	ForceLayoutPrepass();
	const FVector2D CursorPosition = FSlateApplication::Get().GetCursorPos();
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(DetailsPopupHost->Slot))
	{
		if (UPanelWidget* PopupParent = DetailsPopupHost->GetParent())
		{
			const FGeometry& ParentGeometry = PopupParent->GetCachedGeometry();
			const FVector2D ParentSize = ParentGeometry.GetLocalSize();
			if (!ParentSize.IsNearlyZero())
			{
				FVector2D TargetPosition =
					ParentGeometry.AbsoluteToLocal(CursorPosition) + DetailsNodeOffset;
				TargetPosition.X = FMath::Clamp(
					TargetPosition.X,
					0.0f,
					FMath::Max(0.0f, ParentSize.X - DetailsPopupWidth));
				TargetPosition.Y = FMath::Clamp(
					TargetPosition.Y,
					0.0f,
					FMath::Max(0.0f, ParentSize.Y - DetailsPopupMaxHeight));
				CanvasSlot->SetPosition(TargetPosition);
				return;
			}
		}
	}

	const FGeometry& ScreenGeometry = GetCachedGeometry();
	const FGeometry& DetailsGeometry = DetailsPopupHost->GetCachedGeometry();
	if (ScreenGeometry.GetLocalSize().IsNearlyZero())
	{
		return;
	}

	const FVector2D CurrentDetailsLocalPosition =
		ScreenGeometry.AbsoluteToLocal(DetailsGeometry.GetAbsolutePosition());
	FVector2D TargetLocalPosition = ScreenGeometry.AbsoluteToLocal(CursorPosition) + DetailsNodeOffset;

	const FVector2D DetailsSize = DetailsGeometry.GetLocalSize();
	const FVector2D ScreenSize = ScreenGeometry.GetLocalSize();
	TargetLocalPosition.X = FMath::Clamp(
		TargetLocalPosition.X,
		0.0f,
		FMath::Max(0.0f, ScreenSize.X - DetailsSize.X));
	TargetLocalPosition.Y = FMath::Clamp(
		TargetLocalPosition.Y,
		0.0f,
		FMath::Max(0.0f, ScreenSize.Y - DetailsSize.Y));
	DetailsPopupHost->SetRenderTranslation(TargetLocalPosition - CurrentDetailsLocalPosition);
}

void UShipUpgradeScreenWidget::HandleDetailsTooltipHideTimer()
{
	if (!HoveredNodeId.IsNone())
	{
		return;
	}
	if (DetailsPopupHost && DetailsPopupHost->IsHovered())
	{
		bTooltipAwaitingExit = true;
		return;
	}
	HideDetailsTooltip();
}

void UShipUpgradeScreenWidget::HideDetailsTooltip()
{
	bTooltipAwaitingExit = false;
	HoveredNodeId = NAME_None;
	SelectedNodeId = NAME_None;
	if (GraphWidget)
	{
		GraphWidget->SetSelectedNode(NAME_None);
	}
	if (DetailsWidget)
	{
		DetailsWidget->ClearNode();
	}
	SetDetailsPopupVisible(false);
}

void UShipUpgradeScreenWidget::UpdateGraphViewportWidth()
{
	if (!GraphWidget || !WidgetTree)
	{
		return;
	}

	UWidget* GraphAreaBorder = WidgetTree->FindWidget(TEXT("GraphAreaBorder"));
	if (!GraphAreaBorder)
	{
		return;
	}

	ForceLayoutPrepass();
	float ViewportWidth = GraphAreaBorder->GetCachedGeometry().GetLocalSize().X;
	if (ViewportWidth <= KINDA_SMALL_NUMBER)
	{
		if (const UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(GraphAreaBorder->Slot))
		{
			ViewportWidth = CanvasSlot->GetSize().X;
		}
	}

	GraphWidget->SetLayoutViewportWidth(ViewportWidth);
}

void UShipUpgradeScreenWidget::UpdateGraphExtent()
{
	if (!SizeBox_GraphExtent || !GraphWidget)
	{
		return;
	}
	const FVector2D RequiredExtent = GraphWidget->GetRequiredExtent();
	SizeBox_GraphExtent->SetWidthOverride(FMath::Max(VerticalGraphMinWidth, RequiredExtent.X));
	SizeBox_GraphExtent->SetHeightOverride(FMath::Max(VerticalGraphMinHeight, RequiredExtent.Y));
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
