#include "UI/PlayerStatusPreviewStage.h"

#include "Animation/AnimSequenceBase.h"
#include "BaseGameplayTags.h"
#include "BaseItem.h"
#include "BasePlayer.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Equipment/PlayerEquipmentComponent.h"
#include "TimerManager.h"

namespace
{
	const FSoftObjectPath DefaultIdlePath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	const FSoftObjectPath BowIdlePath(TEXT("/Game/Anim_Logic/Anim_Assets/Bow/Standing_Idle_01_Anim.Standing_Idle_01_Anim"));
	const FSoftObjectPath SwordIdlePath(TEXT("/Game/Sword_Anims/Animations/HandsomeSwordV2/Manny_UE5/RootMotion/Idle/Anim_SwordV2_Idle.Anim_SwordV2_Idle"));

	void CopyMeshPresentation(const UMeshComponent* Source, UMeshComponent* Target)
	{
		if (!Source || !Target)
		{
			return;
		}

		Target->SetCastShadow(Source->CastShadow);
		Target->SetReceivesDecals(false);
		for (int32 MaterialIndex = 0; MaterialIndex < Source->GetNumMaterials(); ++MaterialIndex)
		{
			Target->SetMaterial(MaterialIndex, Source->GetMaterial(MaterialIndex));
		}
		Target->SetOverlayMaterial(Source->GetOverlayMaterial());
	}
}

APlayerStatusPreviewStage::APlayerStatusPreviewStage()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PreviewAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("PreviewAnchor"));
	PreviewAnchor->SetupAttachment(SceneRoot);

	PreviewMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PreviewMesh"));
	PreviewMesh->SetupAttachment(PreviewAnchor);
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMesh->SetGenerateOverlapEvents(false);
	PreviewMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("PlayerSceneCapture"));
	SceneCapture->SetupAttachment(SceneRoot);
	SceneCapture->bCaptureEveryFrame = false;
	SceneCapture->bCaptureOnMovement = false;
	SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	SceneCapture->FOVAngle = 30.f;
	SceneCapture->ShowFlags.SetAtmosphere(false);
	SceneCapture->ShowFlags.SetFog(false);
	SceneCapture->ShowFlags.SetVolumetricFog(false);
	SceneCapture->ShowFlags.SetCloud(false);
	SceneCapture->ShowFlags.SetMotionBlur(false);

	KeyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("KeyLight"));
	KeyLight->SetupAttachment(SceneRoot);
	KeyLight->SetIntensityUnits(ELightUnits::Unitless);
	KeyLight->SetUseInverseSquaredFalloff(false);
	KeyLight->SetIntensity(7.f);
	KeyLight->SetAttenuationRadius(3000.f);
	KeyLight->SetLightColor(FLinearColor(1.f, 0.92f, 0.82f));

	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(SceneRoot);
	FillLight->SetIntensityUnits(ELightUnits::Unitless);
	FillLight->SetUseInverseSquaredFalloff(false);
	FillLight->SetIntensity(3.5f);
	FillLight->SetAttenuationRadius(3000.f);
	FillLight->SetLightColor(FLinearColor(0.55f, 0.72f, 1.f));
}

void APlayerStatusPreviewStage::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	EnsureRenderTarget();
}

void APlayerStatusPreviewStage::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearWeaponMeshes();
	SourcePlayer.Reset();
	OwnedRenderTarget = nullptr;
	Super::EndPlay(EndPlayReason);
}

void APlayerStatusPreviewStage::SetSourcePlayer(ABasePlayer* InPlayer)
{
	SourcePlayer = InPlayer;
	if (SourcePlayer.IsValid())
	{
		// Keep the stage inside normal world bounds. Some maps destroy or stop
		// rendering actors placed below their Kill-Z threshold.
		SetActorLocation(SourcePlayer->GetActorLocation() + FVector(0.f, 0.f, 10000.f));
	}
	RefreshFromPlayer();
}

void APlayerStatusPreviewStage::RefreshFromPlayer()
{
	if (!SourcePlayer.IsValid())
	{
		return;
	}

	CopyPlayerMesh();
	CopyEquippedWeapon();
	ApplyPreviewIdle();
	PreviewMesh->TickAnimation(0.f, false);
	PreviewMesh->RefreshBoneTransforms();
	FramePreview();

	if (SceneCapture)
	{
		SceneCapture->CaptureScene();
	}
}

void APlayerStatusPreviewStage::SetPreviewEnabled(bool bEnabled)
{
	if (!SceneCapture)
	{
		return;
	}

	SceneCapture->bCaptureEveryFrame = bEnabled;
	SceneCapture->bAlwaysPersistRenderingState = bEnabled;
	if (bEnabled)
	{
		RefreshFromPlayer();
		FTimerHandle CoverageTimer;
		GetWorldTimerManager().SetTimer(
			CoverageTimer,
			this,
			&APlayerStatusPreviewStage::LogRenderTargetCoverage,
			0.25f,
			false);
	}
}

UTextureRenderTarget2D* APlayerStatusPreviewStage::GetRenderTarget() const
{
	const_cast<APlayerStatusPreviewStage*>(this)->EnsureRenderTarget();
	return SceneCapture ? SceneCapture->TextureTarget : nullptr;
}

void APlayerStatusPreviewStage::EnsureRenderTarget()
{
	if (!SceneCapture || OwnedRenderTarget)
	{
		return;
	}

	OwnedRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("PlayerStatusPreviewRenderTarget"));
	OwnedRenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	OwnedRenderTarget->ClearColor = FLinearColor::Transparent;
	OwnedRenderTarget->InitAutoFormat(768, 1024);
	OwnedRenderTarget->SetFlags(RF_Transient);
	OwnedRenderTarget->ClearFlags(RF_Public | RF_Standalone);
	OwnedRenderTarget->UpdateResourceImmediate(true);
	SceneCapture->TextureTarget = OwnedRenderTarget;
}

void APlayerStatusPreviewStage::CopyPlayerMesh()
{
	USkeletalMeshComponent* SourceMesh = SourcePlayer.IsValid() ? SourcePlayer->GetMesh() : nullptr;
	if (!SourceMesh || !SourceMesh->GetSkeletalMeshAsset())
	{
		PreviewMesh->SetSkeletalMesh(nullptr);
		return;
	}

	PreviewMesh->SetSkeletalMesh(SourceMesh->GetSkeletalMeshAsset());
	PreviewMesh->SetVisibility(true, true);
	PreviewMesh->SetHiddenInGame(false, true);
	PreviewMesh->SetRenderInMainPass(true);
	PreviewMesh->SetRelativeTransform(
		SourceMesh->GetComponentTransform().GetRelativeTransform(SourcePlayer->GetActorTransform()));
	PreviewMesh->SetBoundsScale(SourceMesh->BoundsScale);
	CopyMeshPresentation(SourceMesh, PreviewMesh);

	SceneCapture->ShowOnlyComponents.Reset();
	SceneCapture->ShowOnlyComponents.AddUnique(PreviewMesh);
}

void APlayerStatusPreviewStage::CopyEquippedWeapon()
{
	ClearWeaponMeshes();
	if (!SourcePlayer.IsValid() || !IsValid(SourcePlayer->EquippedItem) || !SourcePlayer->GetMesh())
	{
		return;
	}

	const UPlayerEquipmentComponent* Equipment = SourcePlayer->GetEquipmentComponent();
	const FResolvedEquipmentAttachment Attachment = Equipment
		? Equipment->GetEquippedAttachmentProfile()
		: FResolvedEquipmentAttachment();
	if (!Attachment.IsValid())
	{
		return;
	}

	const FTransform SourceSocketTransform = SourcePlayer->GetMesh()->GetSocketTransform(
		Attachment.CharacterSocketName,
		RTS_World);
	TInlineComponentArray<UMeshComponent*> SourceMeshes(SourcePlayer->EquippedItem);
	for (UMeshComponent* SourceMesh : SourceMeshes)
	{
		if (!SourceMesh || !SourceMesh->IsVisible() || SourceMesh->bHiddenInGame)
		{
			continue;
		}

		UMeshComponent* NewMesh = nullptr;
		if (const UStaticMeshComponent* SourceStaticMesh = Cast<UStaticMeshComponent>(SourceMesh))
		{
			if (SourceStaticMesh->GetStaticMesh())
			{
				UStaticMeshComponent* NewStaticMesh = NewObject<UStaticMeshComponent>(this);
				NewStaticMesh->SetStaticMesh(SourceStaticMesh->GetStaticMesh());
				NewStaticMesh->SetSimulatePhysics(false);
				NewMesh = NewStaticMesh;
			}
		}
		else if (const USkeletalMeshComponent* SourceSkeletalMesh = Cast<USkeletalMeshComponent>(SourceMesh))
		{
			if (SourceSkeletalMesh->GetSkeletalMeshAsset())
			{
				USkeletalMeshComponent* NewSkeletalMesh = NewObject<USkeletalMeshComponent>(this);
				NewSkeletalMesh->SetSkeletalMesh(SourceSkeletalMesh->GetSkeletalMeshAsset());
				NewSkeletalMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
				NewMesh = NewSkeletalMesh;
			}
		}

		if (!NewMesh)
		{
			continue;
		}

		NewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		NewMesh->SetGenerateOverlapEvents(false);
		NewMesh->SetVisibility(true, true);
		NewMesh->SetHiddenInGame(false, true);
		NewMesh->SetRenderInMainPass(true);
		CopyMeshPresentation(SourceMesh, NewMesh);
		NewMesh->SetupAttachment(PreviewMesh, Attachment.CharacterSocketName);
		NewMesh->SetRelativeTransform(SourceMesh->GetComponentTransform().GetRelativeTransform(SourceSocketTransform));
		NewMesh->RegisterComponent();
		ClonedWeaponMeshes.Add(NewMesh);
		SceneCapture->ShowOnlyComponents.AddUnique(NewMesh);
	}
}

void APlayerStatusPreviewStage::ClearWeaponMeshes()
{
	for (UMeshComponent* Mesh : ClonedWeaponMeshes)
	{
		if (IsValid(Mesh))
		{
			if (SceneCapture)
			{
				SceneCapture->ShowOnlyComponents.Remove(Mesh);
			}
			Mesh->DestroyComponent();
		}
	}
	ClonedWeaponMeshes.Reset();
}

void APlayerStatusPreviewStage::ApplyPreviewIdle()
{
	if (!SourcePlayer.IsValid() || !PreviewMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	UAnimSequenceBase* IdleAnimation = nullptr;
	float PlayRate = 1.f;
	if (const UPlayerEquipmentComponent* Equipment = SourcePlayer->GetEquipmentComponent())
	{
		IdleAnimation = Equipment->GetEquippedPreviewIdleAnimation();
		PlayRate = Equipment->GetEquippedPreviewIdlePlayRate();
	}

	if (!IdleAnimation && IsValid(SourcePlayer->EquippedItem))
	{
		const FGameplayTag ItemTag = SourcePlayer->EquippedItem->ItemTag;
		if (ItemTag.MatchesTag(Item_Id_Weapon_Bow))
		{
			IdleAnimation = Cast<UAnimSequenceBase>(BowIdlePath.TryLoad());
		}
		else if (ItemTag.MatchesTag(Item_Id_Weapon_Sword))
		{
			IdleAnimation = Cast<UAnimSequenceBase>(SwordIdlePath.TryLoad());
		}
	}

	if (!IdleAnimation)
	{
		IdleAnimation = Cast<UAnimSequenceBase>(DefaultIdlePath.TryLoad());
	}

	if (!IdleAnimation || IdleAnimation->GetSkeleton() != PreviewMesh->GetSkeletalMeshAsset()->GetSkeleton())
	{
		UE_LOG(LogTemp, Warning, TEXT("Status preview idle is missing or incompatible for %s"), *GetNameSafe(SourcePlayer.Get()));
		return;
	}

	PreviewMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	PreviewMesh->SetAnimation(IdleAnimation);
	PreviewMesh->SetPlayRate(FMath::Max(PlayRate, KINDA_SMALL_NUMBER));
	PreviewMesh->Play(true);
}

void APlayerStatusPreviewStage::FramePreview()
{
	if (!SceneCapture || !PreviewMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	FBox Bounds = PreviewMesh->Bounds.GetBox();
	for (const UMeshComponent* Mesh : ClonedWeaponMeshes)
	{
		if (IsValid(Mesh))
		{
			Bounds += Mesh->Bounds.GetBox();
		}
	}
	if (!Bounds.IsValid)
	{
		return;
	}

	const FVector Center = Bounds.GetCenter();
	const float Radius = FMath::Max(Bounds.GetExtent().Size(), 100.f);
	const float HalfFovRadians = FMath::DegreesToRadians(SceneCapture->FOVAngle * 0.5f);
	const float Distance = Radius / FMath::Max(FMath::Tan(HalfFovRadians), 0.1f) * 1.08f;
	const FVector CameraLocation = Center + FVector(Distance, 0.f, Radius * 0.03f);
	SceneCapture->SetWorldLocation(CameraLocation);
	SceneCapture->SetWorldRotation((Center - CameraLocation).Rotation());
	UE_LOG(LogTemp, Display,
		TEXT("[StatusPreview] Mesh=%s BoundsCenter=%s BoundsExtent=%s Camera=%s ShowOnly=%d"),
		*GetNameSafe(PreviewMesh->GetSkeletalMeshAsset()),
		*Center.ToCompactString(),
		*Bounds.GetExtent().ToCompactString(),
		*CameraLocation.ToCompactString(),
		SceneCapture->ShowOnlyComponents.Num());

	KeyLight->SetWorldLocation(Center + FVector(Radius, -Radius, Radius * 1.2f));
	FillLight->SetWorldLocation(Center + FVector(Radius * 0.2f, Radius, Radius * 0.4f));
}

void APlayerStatusPreviewStage::LogRenderTargetCoverage()
{
	if (!OwnedRenderTarget)
	{
		return;
	}

	FTextureRenderTargetResource* Resource = OwnedRenderTarget->GameThread_GetRenderTargetResource();
	TArray<FLinearColor> Pixels;
	if (!Resource || !Resource->ReadLinearColorPixels(Pixels) || Pixels.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[StatusPreview] Render target pixel readback failed"));
		return;
	}

	int32 ColoredPixels = 0;
	int32 CharacterAlphaPixels = 0;
	for (const FLinearColor& Pixel : Pixels)
	{
		if (FMath::Max3(Pixel.R, Pixel.G, Pixel.B) > 0.01f)
		{
			++ColoredPixels;
		}
		// SceneColorHDR stores inverse opacity in alpha.
		if (Pixel.A < 0.99f)
		{
			++CharacterAlphaPixels;
		}
	}

	UE_LOG(LogTemp, Display,
		TEXT("[StatusPreview] RenderTargetPixels=%d Colored=%d CharacterAlpha=%d"),
		Pixels.Num(),
		ColoredPixels,
		CharacterAlphaPixels);
}
