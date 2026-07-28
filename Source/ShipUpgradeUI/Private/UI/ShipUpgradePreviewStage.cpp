#include "UI/ShipUpgradePreviewStage.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"

AShipUpgradePreviewStage::AShipUpgradePreviewStage()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PreviewAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("PreviewAnchor"));
	PreviewAnchor->SetupAttachment(SceneRoot);

	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
	SceneCapture->SetupAttachment(SceneRoot);
	SceneCapture->bCaptureEveryFrame = false;
	SceneCapture->bCaptureOnMovement = false;
	SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	SceneCapture->FOVAngle = 35.0f;
	SceneCapture->ShowFlags.SetAtmosphere(false);
	SceneCapture->ShowFlags.SetFog(false);
	SceneCapture->ShowFlags.SetVolumetricFog(false);
	SceneCapture->ShowFlags.SetCloud(false);

	KeyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("KeyLight"));
	KeyLight->SetupAttachment(SceneRoot);
	KeyLight->SetIntensityUnits(ELightUnits::Unitless);
	KeyLight->SetUseInverseSquaredFalloff(false);
	KeyLight->SetIntensity(8.0f);
	KeyLight->SetAttenuationRadius(5000.0f);
	KeyLight->SetLightColor(FLinearColor(1.0f, 0.92f, 0.8f));

	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(SceneRoot);
	FillLight->SetIntensityUnits(ELightUnits::Unitless);
	FillLight->SetUseInverseSquaredFalloff(false);
	FillLight->SetIntensity(4.0f);
	FillLight->SetAttenuationRadius(5000.0f);
	FillLight->SetLightColor(FLinearColor(0.55f, 0.75f, 1.0f));
}

void AShipUpgradePreviewStage::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	EnsureOwnedRenderTarget();
}

void AShipUpgradePreviewStage::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ActorClassLoadHandle.IsValid())
	{
		ActorClassLoadHandle->CancelHandle();
		ActorClassLoadHandle.Reset();
	}
	ClearPreviewActor();
	ClearClonedPreviewComponents();
	OwnedRenderTarget = nullptr;
	Super::EndPlay(EndPlayReason);
}

void AShipUpgradePreviewStage::SetPreviewActorSoftClass(TSoftClassPtr<AActor> InActorClass)
{
	if (ActorClassLoadHandle.IsValid())
	{
		ActorClassLoadHandle->CancelHandle();
		ActorClassLoadHandle.Reset();
	}

	PendingActorClassPath = InActorClass.ToSoftObjectPath();
	if (!PendingActorClassPath.IsValid())
	{
		ClearPreviewActor();
		return;
	}

	if (UClass* LoadedClass = InActorClass.Get())
	{
		SetPreviewActorClass(LoadedClass);
		return;
	}

	const FSoftObjectPath RequestedPath = PendingActorClassPath;
	TWeakObjectPtr<AShipUpgradePreviewStage> WeakThis(this);
	ActorClassLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		RequestedPath,
		FStreamableDelegate::CreateLambda([WeakThis, RequestedPath]()
		{
			AShipUpgradePreviewStage* Stage = WeakThis.Get();
			if (!Stage || Stage->PendingActorClassPath != RequestedPath)
			{
				return;
			}
			if (UClass* LoadedClass = Cast<UClass>(RequestedPath.ResolveObject()))
			{
				Stage->SetPreviewActorClass(LoadedClass);
			}
		}));
}

void AShipUpgradePreviewStage::SetPreviewActorClass(TSubclassOf<AActor> InActorClass)
{
	PendingActorClassPath.Reset();
	ClearPreviewActor();
	ClearClonedPreviewComponents();
	if (!InActorClass || !GetWorld() || !PreviewAnchor)
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	SpawnedPreviewActor = GetWorld()->SpawnActor<AActor>(
		InActorClass,
		PreviewAnchor->GetComponentTransform(),
		SpawnParameters);
	if (SpawnedPreviewActor)
	{
		SpawnedPreviewActor->SetActorEnableCollision(false);
		SpawnedPreviewActor->SetActorTickEnabled(false);
		SpawnedPreviewActor->AttachToComponent(
			PreviewAnchor,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale);

		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(SpawnedPreviewActor);
		for (UPrimitiveComponent* Component : PrimitiveComponents)
		{
			if (!Component)
			{
				continue;
			}
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			if (UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Component))
			{
				StaticMesh->SetSimulatePhysics(false);
			}
			if (Component->IsVisible() && !Component->bHiddenInGame)
			{
				SceneCapture->ShowOnlyComponents.AddUnique(Component);
			}
		}
		FramePreview();
		SceneCapture->CaptureScene();
	}
}

void AShipUpgradePreviewStage::SetPreviewSourceActor(AActor* InSourceActor)
{
	if (ActorClassLoadHandle.IsValid())
	{
		ActorClassLoadHandle->CancelHandle();
		ActorClassLoadHandle.Reset();
	}
	PendingActorClassPath.Reset();
	ClearPreviewActor();
	ClearClonedPreviewComponents();
	if (!IsValid(InSourceActor) || !PreviewAnchor)
	{
		return;
	}

	CloneVisibleMeshes(InSourceActor, InSourceActor);
	TArray<AActor*> AttachedActors;
	InSourceActor->GetAttachedActors(AttachedActors, true, true);
	for (AActor* AttachedActor : AttachedActors)
	{
		CloneVisibleMeshes(InSourceActor, AttachedActor);
	}

	FramePreview();
	SceneCapture->CaptureScene();
}

void AShipUpgradePreviewStage::ClearPreviewActor()
{
	if (IsValid(SpawnedPreviewActor))
	{
		SpawnedPreviewActor->Destroy();
	}
	SpawnedPreviewActor = nullptr;
}

void AShipUpgradePreviewStage::AddPreviewYaw(float DeltaYaw)
{
	if (PreviewAnchor)
	{
		PreviewAnchor->AddLocalRotation(FRotator(0.0f, DeltaYaw, 0.0f));
		if (SceneCapture)
		{
			SceneCapture->CaptureScene();
		}
	}
}

UTextureRenderTarget2D* AShipUpgradePreviewStage::GetRenderTarget() const
{
	const_cast<AShipUpgradePreviewStage*>(this)->EnsureOwnedRenderTarget();
	return SceneCapture ? SceneCapture->TextureTarget : nullptr;
}

void AShipUpgradePreviewStage::EnsureOwnedRenderTarget()
{
	if (!SceneCapture)
	{
		return;
	}
	// Blueprint component defaults from older assets may still contain
	// FinalColorLDR/CaptureEveryFrame. Reassert the runtime contract here,
	// after Blueprint serialization, before the target is exposed to UMG.
	SceneCapture->bCaptureEveryFrame = false;
	SceneCapture->bCaptureOnMovement = false;
	SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	SceneCapture->ShowFlags.SetAtmosphere(false);
	SceneCapture->ShowFlags.SetFog(false);
	SceneCapture->ShowFlags.SetVolumetricFog(false);
	SceneCapture->ShowFlags.SetCloud(false);
	if (OwnedRenderTarget)
	{
		return;
	}

	if (UTextureRenderTarget2D* Template = SceneCapture->TextureTarget)
	{
		OwnedRenderTarget = DuplicateObject<UTextureRenderTarget2D>(Template, this);
	}
	else
	{
		OwnedRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("ShipUpgradePreviewRenderTarget"));
		OwnedRenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
		OwnedRenderTarget->ClearColor = FLinearColor::Transparent;
		OwnedRenderTarget->InitAutoFormat(1024, 1024);
	}

	if (OwnedRenderTarget)
	{
		OwnedRenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
		OwnedRenderTarget->ClearColor = FLinearColor::Transparent;
		OwnedRenderTarget->SetFlags(RF_Transient);
		OwnedRenderTarget->ClearFlags(RF_Public | RF_Standalone);
		OwnedRenderTarget->UpdateResourceImmediate(true);
		SceneCapture->TextureTarget = OwnedRenderTarget;
	}
}

void AShipUpgradePreviewStage::ClearClonedPreviewComponents()
{
	if (SceneCapture)
	{
		SceneCapture->ShowOnlyComponents.Reset();
	}
	for (UPrimitiveComponent* Component : ClonedPreviewComponents)
	{
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}
	ClonedPreviewComponents.Reset();
	if (PreviewAnchor)
	{
		PreviewAnchor->SetRelativeRotation(FRotator::ZeroRotator);
	}
}

void AShipUpgradePreviewStage::CloneVisibleMeshes(AActor* SourceRoot, AActor* ActorToCopy)
{
	if (!IsValid(SourceRoot) || !IsValid(ActorToCopy))
	{
		return;
	}

	const FTransform SourceRootTransform = SourceRoot->GetActorTransform();
	TInlineComponentArray<UMeshComponent*> MeshComponents(ActorToCopy);
	for (UMeshComponent* SourceMesh : MeshComponents)
	{
		if (!SourceMesh || !SourceMesh->IsVisible() || SourceMesh->bHiddenInGame)
		{
			continue;
		}

		UMeshComponent* NewMesh = nullptr;
		if (UStaticMeshComponent* SourceStaticMesh = Cast<UStaticMeshComponent>(SourceMesh))
		{
			if (!SourceStaticMesh->GetStaticMesh())
			{
				continue;
			}
			UStaticMeshComponent* NewStaticMesh = NewObject<UStaticMeshComponent>(this);
			NewStaticMesh->SetStaticMesh(SourceStaticMesh->GetStaticMesh());
			NewStaticMesh->SetSimulatePhysics(false);
			NewMesh = NewStaticMesh;
		}
		else if (USkeletalMeshComponent* SourceSkeletalMesh = Cast<USkeletalMeshComponent>(SourceMesh))
		{
			if (!SourceSkeletalMesh->GetSkeletalMeshAsset())
			{
				continue;
			}
			USkeletalMeshComponent* NewSkeletalMesh = NewObject<USkeletalMeshComponent>(this);
			NewSkeletalMesh->SetSkeletalMesh(SourceSkeletalMesh->GetSkeletalMeshAsset());
			NewMesh = NewSkeletalMesh;
		}

		if (!NewMesh)
		{
			continue;
		}

		NewMesh->SetupAttachment(PreviewAnchor);
		NewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		NewMesh->SetCastShadow(SourceMesh->CastShadow);
		for (int32 MaterialIndex = 0; MaterialIndex < SourceMesh->GetNumMaterials(); ++MaterialIndex)
		{
			NewMesh->SetMaterial(MaterialIndex, SourceMesh->GetMaterial(MaterialIndex));
		}
		NewMesh->SetRelativeTransform(
			SourceMesh->GetComponentTransform().GetRelativeTransform(SourceRootTransform));
		NewMesh->RegisterComponent();
		ClonedPreviewComponents.Add(NewMesh);
		SceneCapture->ShowOnlyComponents.AddUnique(NewMesh);
	}
}

void AShipUpgradePreviewStage::FramePreview()
{
	if (!SceneCapture)
	{
		return;
	}

	FBox Bounds(ForceInit);
	if (IsValid(SpawnedPreviewActor))
	{
		Bounds += SpawnedPreviewActor->GetComponentsBoundingBox(true);
	}
	for (UPrimitiveComponent* Component : ClonedPreviewComponents)
	{
		if (IsValid(Component))
		{
			Bounds += Component->Bounds.GetBox();
		}
	}
	if (!Bounds.IsValid)
	{
		return;
	}

	const FVector Center = Bounds.GetCenter();
	const float Radius = FMath::Max(Bounds.GetExtent().Size(), 100.0f);
	const float HalfFovRadians = FMath::DegreesToRadians(SceneCapture->FOVAngle * 0.5f);
	const float Distance = Radius / FMath::Max(FMath::Tan(HalfFovRadians), 0.1f) * 1.15f;
	const FVector CameraLocation = Center + FVector(-Distance, -Distance * 0.7f, Distance * 0.32f);
	SceneCapture->SetWorldLocation(CameraLocation);
	SceneCapture->SetWorldRotation((Center - CameraLocation).Rotation());
	if (KeyLight)
	{
		KeyLight->SetAttenuationRadius(FMath::Max(Radius * 8.0f, 1000.0f));
		KeyLight->SetWorldLocation(Center + FVector(-Radius, -Radius, Radius * 1.5f));
	}
	if (FillLight)
	{
		FillLight->SetAttenuationRadius(FMath::Max(Radius * 8.0f, 1000.0f));
		FillLight->SetWorldLocation(Center + FVector(Radius, Radius, Radius * 0.5f));
	}
}
