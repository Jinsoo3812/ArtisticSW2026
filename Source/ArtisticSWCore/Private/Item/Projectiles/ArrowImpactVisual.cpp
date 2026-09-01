#include "Item/Projectiles/ArrowImpactVisual.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Item/Projectiles/ArrowProjectile.h"

AArrowImpactVisual::AArrowImpactVisual()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetReplicateMovement(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	ArrowMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ArrowMesh"));
	ArrowMesh->SetupAttachment(SceneRoot);
	ArrowMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ArrowMesh->SetGenerateOverlapEvents(false);
	ArrowMesh->SetCanEverAffectNavigation(false);
}

void AArrowImpactVisual::InitializeFromProjectile(
	const AArrowProjectile& SourceProjectile,
	const FArrowImpactPresentationData& ImpactData,
	float EmbedDepth,
	float VisualLifeSpan)
{
	if (!SourceProjectile.ApplyVisualTo(ArrowMesh))
	{
		Destroy();
		return;
	}

	const FVector IncomingDirection = FVector(ImpactData.IncomingDirection).GetSafeNormal();
	if (IncomingDirection.IsNearlyZero())
	{
		Destroy();
		return;
	}

	const float CenterBehindImpact = FMath::Max(
		0.0f,
		SourceProjectile.GetCollisionHalfExtent().X - FMath::Max(0.0f, EmbedDepth));
	const FVector VisualLocation = FVector(ImpactData.ImpactLocation) - IncomingDirection * CenterBehindImpact;
	SetActorLocationAndRotation(VisualLocation, IncomingDirection.Rotation(), false, nullptr, ETeleportType::TeleportPhysics);

	if (USceneComponent* AttachComponent = ImpactData.AttachComponent.Get())
	{
		AttachToComponent(
			AttachComponent,
			FAttachmentTransformRules::KeepWorldTransform,
			ImpactData.BoneName);
	}

	SetLifeSpan(FMath::Max(0.1f, VisualLifeSpan));
}
