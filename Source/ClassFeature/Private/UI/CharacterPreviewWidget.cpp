#include "UI/CharacterPreviewWidget.h"

#include "BasePlayer.h"
#include "BaseItem.h"
#include "Components/Image.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"

void UCharacterPreviewWidget::InitializeForPlayer(ABasePlayer* InPlayer)
{
	if (CachedPlayer.IsValid())
	{
		CachedPlayer->OnItemSlotsChanged.RemoveAll(this);
		CachedPlayer->OnQuickSlotsChanged.RemoveAll(this);
	}

	if (CachedPlayer.Get() != InPlayer)
	{
		DestroyCaptureResources();
	}

	CachedPlayer = InPlayer;
	if (CachedPlayer.IsValid())
	{
		CachedPlayer->OnItemSlotsChanged.AddUObject(this, &UCharacterPreviewWidget::RefreshPreview);
		CachedPlayer->OnQuickSlotsChanged.AddUObject(this, &UCharacterPreviewWidget::RefreshPreview);
	}
	RefreshPreview();
}

void UCharacterPreviewWidget::RefreshPreview()
{
	if (!CharacterPreviewImage || !CachedPlayer.IsValid())
	{
		return;
	}

	CreateCaptureResources();
	if (!PreviewCaptureActor || !PreviewCaptureActor->GetCaptureComponent2D())
	{
		return;
	}

	USceneCaptureComponent2D* CaptureComponent = PreviewCaptureActor->GetCaptureComponent2D();
	CaptureComponent->ClearShowOnlyComponents();
	CaptureComponent->ShowOnlyActorComponents(CachedPlayer.Get(), true);
	if (IsValid(CachedPlayer->EquippedItem))
	{
		CaptureComponent->ShowOnlyActorComponents(CachedPlayer->EquippedItem, true);
	}
	CaptureComponent->bCaptureEveryFrame = bPreviewActive;
	CaptureComponent->bCaptureOnMovement = bPreviewActive;
	if (bPreviewActive)
	{
		CaptureComponent->CaptureScene();
	}
}

void UCharacterPreviewWidget::SetPreviewActive(bool bActive)
{
	bPreviewActive = bActive;
	if (PreviewCaptureActor && PreviewCaptureActor->GetCaptureComponent2D())
	{
		USceneCaptureComponent2D* CaptureComponent = PreviewCaptureActor->GetCaptureComponent2D();
		CaptureComponent->bCaptureEveryFrame = bPreviewActive;
		CaptureComponent->bCaptureOnMovement = bPreviewActive;
		if (bPreviewActive)
		{
			RefreshPreview();
		}
	}
}

void UCharacterPreviewWidget::CreateCaptureResources()
{
	if (!CachedPlayer.IsValid() || !CharacterPreviewImage)
	{
		return;
	}

	if (!PreviewRenderTarget)
	{
		PreviewRenderTarget = NewObject<UTextureRenderTarget2D>(this);
		PreviewRenderTarget->ClearColor = BackgroundColor;
		PreviewRenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8_SRGB;
		PreviewRenderTarget->InitAutoFormat(RenderTargetResolution, RenderTargetResolution);
		PreviewRenderTarget->UpdateResourceImmediate(true);
		CharacterPreviewImage->SetBrushResourceObject(PreviewRenderTarget);
	}

	if (!PreviewCaptureActor)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PreviewCaptureActor = CachedPlayer->GetWorld()->SpawnActor<ASceneCapture2D>(SpawnParameters);
		if (!PreviewCaptureActor)
		{
			return;
		}

		PreviewCaptureActor->SetActorEnableCollision(false);
		PreviewCaptureActor->AttachToComponent(CachedPlayer->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		PreviewCaptureActor->SetActorRelativeLocation(CameraRelativeLocation);
		PreviewCaptureActor->SetActorRelativeRotation(CameraRelativeRotation);

		USceneCaptureComponent2D* CaptureComponent = PreviewCaptureActor->GetCaptureComponent2D();
		CaptureComponent->TextureTarget = PreviewRenderTarget;
		CaptureComponent->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		CaptureComponent->FOVAngle = CameraFieldOfView;
		CaptureComponent->bCaptureEveryFrame = bPreviewActive;
		CaptureComponent->bCaptureOnMovement = bPreviewActive;
	}
}

void UCharacterPreviewWidget::DestroyCaptureResources()
{
	if (PreviewCaptureActor)
	{
		PreviewCaptureActor->Destroy();
		PreviewCaptureActor = nullptr;
	}
	PreviewRenderTarget = nullptr;
}

void UCharacterPreviewWidget::NativeDestruct()
{
	if (CachedPlayer.IsValid())
	{
		CachedPlayer->OnItemSlotsChanged.RemoveAll(this);
		CachedPlayer->OnQuickSlotsChanged.RemoveAll(this);
	}
	DestroyCaptureResources();
	Super::NativeDestruct();
}
