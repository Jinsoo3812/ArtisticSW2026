#include "Skills/AreaSlowDecalActors.h"

#include "Components/DecalComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

namespace AreaSlowDecal
{
	void ApplyConfig(
		UDecalComponent* Decal,
		const FVector& BoxExtent,
		float ProjectionDepth,
		UMaterialInterface* Material)
	{
		if (!Decal)
		{
			return;
		}

		// Decal local X is projection depth; Y/Z are the projected rectangle axes.
		Decal->DecalSize = FVector(
			FMath::Max(1.0f, ProjectionDepth),
			FMath::Max(1.0f, BoxExtent.Y),
			FMath::Max(1.0f, BoxExtent.X));
		if (Material)
		{
			Decal->SetDecalMaterial(Material);
		}
		Decal->MarkRenderStateDirty();
	}
}

AAreaSlowTargetingDecal::AAreaSlowTargetingDecal()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetActorEnableCollision(false);

	PreviewRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PreviewRoot"));
	SetRootComponent(PreviewRoot);

	Decal = CreateDefaultSubobject<UDecalComponent>(TEXT("RangeDecal"));
	Decal->SetupAttachment(PreviewRoot);
	Decal->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
	Decal->SetFadeScreenSize(0.001f);
}

void AAreaSlowTargetingDecal::ConfigurePreview(
	AActor* InSourceActor,
	UAreaSlowSkillDataAsset* InSkillData)
{
	SourceActor = InSourceActor;
	SkillData = InSkillData;
	if (SkillData)
	{
		AreaSlowDecal::ApplyConfig(
			Decal,
			FVector(
				SkillData->RangeLength * 0.5f,
				SkillData->RangeWidth * 0.5f,
				SkillData->RangeHeight * 0.5f),
			SkillData->DecalProjectionDepth,
			SkillData->TargetingDecalMaterial);
	}
	RefreshFromSource();
}

void AAreaSlowTargetingDecal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	RefreshFromSource();
}

void AAreaSlowTargetingDecal::RefreshFromSource()
{
	if (!SourceActor.IsValid() || !SkillData)
	{
		Destroy();
		return;
	}

	const FAreaSlowRange Range = SkillData->BuildRangeForActor(SourceActor.Get());
	SetActorLocationAndRotation(Range.Center, Range.Rotation);
}

AAreaSlowConfirmedDecal::AAreaSlowConfirmedDecal()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	bNetLoadOnClient = false;
	SetReplicateMovement(false);
	SetActorEnableCollision(false);

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	SetRootComponent(VisualRoot);

	Decal = CreateDefaultSubobject<UDecalComponent>(TEXT("RangeDecal"));
	Decal->SetupAttachment(VisualRoot);
	Decal->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
	Decal->SetFadeScreenSize(0.001f);
}

void AAreaSlowConfirmedDecal::InitializeConfirmedVisual(
	const FAreaSlowRange& InRange,
	float InProjectionDepth,
	UMaterialInterface* InMaterial,
	float InDuration)
{
	if (!HasAuthority())
	{
		return;
	}

	SetActorLocationAndRotation(InRange.Center, InRange.Rotation);
	ReplicatedBoxExtent = InRange.BoxExtent;
	ReplicatedProjectionDepth = FMath::Max(1.0f, InProjectionDepth);
	ReplicatedMaterial = InMaterial;
	ApplyVisualConfig();
	SetLifeSpan(FMath::Max(0.05f, InDuration));
	ForceNetUpdate();
}

void AAreaSlowConfirmedDecal::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAreaSlowConfirmedDecal, ReplicatedBoxExtent);
	DOREPLIFETIME(AAreaSlowConfirmedDecal, ReplicatedProjectionDepth);
	DOREPLIFETIME(AAreaSlowConfirmedDecal, ReplicatedMaterial);
}

void AAreaSlowConfirmedDecal::OnRep_VisualConfig()
{
	ApplyVisualConfig();
}

void AAreaSlowConfirmedDecal::ApplyVisualConfig()
{
	AreaSlowDecal::ApplyConfig(
		Decal,
		ReplicatedBoxExtent,
		ReplicatedProjectionDepth,
		ReplicatedMaterial);
}
