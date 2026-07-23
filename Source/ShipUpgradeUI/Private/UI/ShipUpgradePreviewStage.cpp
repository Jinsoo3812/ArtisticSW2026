#include "UI/ShipUpgradePreviewStage.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/TextureRenderTarget2D.h"

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
	SceneCapture->bCaptureEveryFrame = true;
	SceneCapture->bCaptureOnMovement = true;
}

void AShipUpgradePreviewStage::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ActorClassLoadHandle.IsValid())
	{
		ActorClassLoadHandle->CancelHandle();
		ActorClassLoadHandle.Reset();
	}
	ClearPreviewActor();
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
	ClearPreviewActor();
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
		SpawnedPreviewActor->AttachToComponent(
			PreviewAnchor,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
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
	if (IsValid(SpawnedPreviewActor))
	{
		SpawnedPreviewActor->AddActorLocalRotation(FRotator(0.0f, DeltaYaw, 0.0f));
	}
}

UTextureRenderTarget2D* AShipUpgradePreviewStage::GetRenderTarget() const
{
	return SceneCapture ? SceneCapture->TextureTarget : nullptr;
}
