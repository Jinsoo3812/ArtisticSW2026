#include "GameplayCue/SWPathGameplayCueNotify.h"

#include "Components/DecalComponent.h"
#include "Components/SceneComponent.h"
#include "GAS/SWCombatEffectContextLibrary.h"
#include "NiagaraComponent.h"

ASWPathGameplayCueNotify::ASWPathGameplayCueNotify()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bAutoDestroyOnRemove = true;
	bAutoAttachToOwner = false;
	bUniqueInstancePerInstigator = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PathDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("PathDecal"));
	PathDecal->SetupAttachment(SceneRoot);
	PathDecal->SetVisibility(false);

	PathNiagara = CreateDefaultSubobject<UNiagaraComponent>(TEXT("PathNiagara"));
	PathNiagara->SetupAttachment(SceneRoot);
	PathNiagara->SetAutoActivate(false);
}

void ASWPathGameplayCueNotify::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdatePathTransform();
}

bool ASWPathGameplayCueNotify::OnActive_Implementation(
	AActor* MyTarget,
	const FGameplayCueParameters& Parameters)
{
	return InitializePath(Parameters);
}

bool ASWPathGameplayCueNotify::WhileActive_Implementation(
	AActor* MyTarget,
	const FGameplayCueParameters& Parameters)
{
	return InitializePath(Parameters);
}

bool ASWPathGameplayCueNotify::OnRemove_Implementation(
	AActor* MyTarget,
	const FGameplayCueParameters& Parameters)
{
	ResetPath();
	return true;
}

bool ASWPathGameplayCueNotify::Recycle()
{
	ResetPath();
	return Super::Recycle();
}

bool ASWPathGameplayCueNotify::InitializePath(const FGameplayCueParameters& Parameters)
{
	FSWPathCuePayload Payload;
	if (!USWCombatEffectContextLibrary::GetPathCuePayload(Parameters.EffectContext, Payload))
	{
		ResetPath();
		return false;
	}

	const bool bNewPath = !bPathInitialized || ActivePath.InstanceId != Payload.InstanceId;
	ActivePath = Payload;
	bPathInitialized = true;
	SetActorTickEnabled(true);
	if (PathDecal)
	{
		PathDecal->SetVisibility(true);
	}
	if (PathNiagara && !PathNiagara->IsActive())
	{
		PathNiagara->Activate(true);
	}
	UpdatePathTransform();
	if (bNewPath)
	{
		OnPathInitialized(ActivePath);
	}
	return true;
}

void ASWPathGameplayCueNotify::UpdatePathTransform()
{
	AActor* ReferenceActor = ActivePath.ReferenceActor.Get();
	if (!bPathInitialized || !IsValid(ReferenceActor))
	{
		ResetPath();
		return;
	}

	const FTransform& ReferenceTransform = ReferenceActor->GetActorTransform();
	const FVector StartWorld = ReferenceTransform.TransformPosition(FVector(ActivePath.StartLocal));
	const FVector EndWorld = ReferenceTransform.TransformPosition(FVector(ActivePath.EndLocal));
	const FVector SurfaceNormal = ReferenceTransform.TransformVectorNoScale(
		FVector(ActivePath.SurfaceNormalLocal)).GetSafeNormal();
	const FVector PathDirection = (EndWorld - StartWorld).GetSafeNormal();
	const float PathLength = FVector::Distance(StartWorld, EndWorld);
	if (PathDirection.IsNearlyZero() || SurfaceNormal.IsNearlyZero())
	{
		ResetPath();
		return;
	}

	const FVector Midpoint = (StartWorld + EndWorld) * 0.5f + SurfaceNormal * SurfaceOffset;
	const FRotator Rotation = FRotationMatrix::MakeFromXZ(-SurfaceNormal, PathDirection).Rotator();
	SetActorLocationAndRotation(Midpoint, Rotation);

	const float HalfWidth = VisualWidthOverride > 0.0f
		? VisualWidthOverride * 0.5f
		: FMath::Max(1.0f, ActivePath.CorridorRadius);
	if (PathDecal)
	{
		PathDecal->DecalSize = FVector(
			FMath::Max(1.0f, ProjectionDepth), HalfWidth, FMath::Max(1.0f, PathLength * 0.5f));
	}
	if (PathNiagara)
	{
		PathNiagara->SetVariableVec3(NiagaraStartParameter, StartWorld);
		PathNiagara->SetVariableVec3(NiagaraEndParameter, EndWorld);
		PathNiagara->SetVariableFloat(NiagaraWidthParameter, HalfWidth * 2.0f);
	}
	OnPathTransformUpdated(StartWorld, EndWorld, HalfWidth);
}

void ASWPathGameplayCueNotify::ResetPath()
{
	bPathInitialized = false;
	ActivePath = FSWPathCuePayload();
	SetActorTickEnabled(false);
	if (PathDecal)
	{
		PathDecal->SetVisibility(false);
	}
	if (PathNiagara)
	{
		PathNiagara->Deactivate();
	}
}
