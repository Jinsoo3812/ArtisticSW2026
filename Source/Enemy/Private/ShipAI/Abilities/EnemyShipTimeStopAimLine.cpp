#include "ShipAI/Abilities/EnemyShipTimeStopAimLine.h"

#include "Cannon.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Ship.h"
#include "Effects/SWNiagaraScaleLibrary.h"
#include "UObject/ConstructorHelpers.h"

AEnemyShipTimeStopAimLine::AEnemyShipTimeStopAimLine()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(20.0f);
	SetMinNetUpdateFrequency(10.0f);
	SetReplicateMovement(false);
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	LineMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LineMesh"));
	LineMesh->SetupAttachment(SceneRoot);
	LineMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LineMesh->SetGenerateOverlapEvents(false);
	LineMesh->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderFinder.Succeeded())
	{
		LineMesh->SetStaticMesh(CylinderFinder.Object);
	}
	// Engine Cylinder is Z-aligned. Rotate it so its length follows this actor's X axis.
	LineMesh->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));

	ChargeEffectComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ChargeEffectComponent"));
	ChargeEffectComponent->SetupAttachment(SceneRoot);
	ChargeEffectComponent->SetAutoActivate(false);
}

void AEnemyShipTimeStopAimLine::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		SetActorTickEnabled(false);
	}
}

void AEnemyShipTimeStopAimLine::InitializeAimLine(
	const FVector& InStart,
	const FVector& InDirection,
	AShip* InTargetShip,
	float InMaximumDistance,
	float InTraceIntervalSeconds)
{
	if (!HasAuthority())
	{
		return;
	}
	LineStart = InStart;
	SourceCannon.Reset();
	FixedDirection = InDirection.GetSafeNormal();
	TargetShip = InTargetShip;
	MaximumDistance = FMath::Max(1.0f, InMaximumDistance);
	TraceIntervalSeconds = FMath::Max(0.01f, InTraceIntervalSeconds);
	TraceTimeAccumulator = 0.0f;
	UpdateClippedEndpoint();
}

void AEnemyShipTimeStopAimLine::LockAimTargetPoint(const FVector& InWorldTargetPoint)
{
	if (!HasAuthority())
	{
		return;
	}
	const FVector Direction = (InWorldTargetPoint - FVector(LineStart)).GetSafeNormal();
	if (!Direction.IsNearlyZero())
	{
		LockedTargetPoint = InWorldTargetPoint;
		FixedDirection = Direction;
		bAimTargetLocked = true;
		UpdateClippedEndpoint();
	}
}

void AEnemyShipTimeStopAimLine::BeginLockedCharge(
	UNiagaraSystem* InChargeEffect,
	float InUniformScale)
{
	if (!HasAuthority())
	{
		return;
	}
	ChargeEffect = InChargeEffect;
	ChargeEffectScale = FMath::Max(0.01f, InUniformScale);
	bChargeEffectActive = ChargeEffect != nullptr;
	RefreshChargeEffect();
	ForceNetUpdate();
}

void AEnemyShipTimeStopAimLine::PlayInstantHitEffects(
	UNiagaraSystem* InTrailEffect,
	UNiagaraSystem* InExplosionEffect,
	const FVector& InStart,
	const FVector& InEnd,
	bool bHitPlayer,
	float InTrailScale,
	float InTrailLifetimeSeconds,
	float InExplosionScale,
	float InPresentationLifetime)
{
	if (!HasAuthority())
	{
		return;
	}
	bChargeEffectActive = false;
	RefreshChargeEffect();
	bWarningLineVisible = false;
	OnRep_LineVisibility();
	SetActorTickEnabled(false);
	MulticastPlayInstantHitEffects(
		InTrailEffect,
		InExplosionEffect,
		InStart,
		InEnd,
		bHitPlayer,
		InTrailScale,
		InTrailLifetimeSeconds,
		InExplosionScale);
	ForceNetUpdate();
	SetLifeSpan(FMath::Max(0.1f, InPresentationLifetime));
}

void AEnemyShipTimeStopAimLine::InitializeAimLineFromCannon(
	ACannon* InSourceCannon,
	AShip* InTargetShip,
	float InMaximumDistance,
	float InTraceIntervalSeconds)
{
	if (!HasAuthority() || !InSourceCannon)
	{
		return;
	}
	SourceCannon = InSourceCannon;
	LineStart = InSourceCannon->GetProjectileMuzzleTransform().GetLocation();
	TargetShip = InTargetShip;
	const FVector TargetCenter = InTargetShip && InTargetShip->BuoyancyRoot
		? InTargetShip->BuoyancyRoot->GetComponentLocation()
		: (InTargetShip ? InTargetShip->GetActorLocation() : FVector(LineStart));
	FixedDirection = (TargetCenter - FVector(LineStart)).GetSafeNormal();
	MaximumDistance = FMath::Max(1.0f, InMaximumDistance);
	TraceIntervalSeconds = FMath::Max(0.01f, InTraceIntervalSeconds);
	TraceTimeAccumulator = 0.0f;
	UpdateClippedEndpoint();
}

void AEnemyShipTimeStopAimLine::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}
	TraceTimeAccumulator += DeltaSeconds;
	if (TraceTimeAccumulator >= TraceIntervalSeconds)
	{
		TraceTimeAccumulator = FMath::Fmod(TraceTimeAccumulator, TraceIntervalSeconds);
		UpdateClippedEndpoint();
	}
}

FVector AEnemyShipTimeStopAimLine::ResolveClippedLineEnd(
	const FVector& InStart,
	const FVector& InDirection,
	const AShip* InTargetShip,
	float InMaximumDistance)
{
	const FVector Direction = InDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return InStart;
	}
	const FVector FarEnd = InStart + Direction * FMath::Max(1.0f, InMaximumDistance);
	FHitResult Hit;
	if (InTargetShip && InTargetShip->ShipDamageMesh
		&& InTargetShip->ShipDamageMesh->LineTraceComponent(
			Hit, InStart, FarEnd, FCollisionQueryParams()))
	{
		return Hit.ImpactPoint;
	}
	return FarEnd;
}

void AEnemyShipTimeStopAimLine::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEnemyShipTimeStopAimLine, LineStart);
	DOREPLIFETIME(AEnemyShipTimeStopAimLine, LineEnd);
	DOREPLIFETIME(AEnemyShipTimeStopAimLine, bWarningLineVisible);
	DOREPLIFETIME(AEnemyShipTimeStopAimLine, ChargeEffect);
	DOREPLIFETIME(AEnemyShipTimeStopAimLine, ChargeEffectScale);
	DOREPLIFETIME(AEnemyShipTimeStopAimLine, bChargeEffectActive);
}

void AEnemyShipTimeStopAimLine::OnRep_LineEndpoints()
{
	RefreshLineVisual();
}

void AEnemyShipTimeStopAimLine::OnRep_LineVisibility()
{
	if (LineMesh)
	{
		LineMesh->SetVisibility(bWarningLineVisible);
	}
}

void AEnemyShipTimeStopAimLine::OnRep_ChargeState()
{
	RefreshChargeEffect();
}

void AEnemyShipTimeStopAimLine::UpdateClippedEndpoint()
{
	bool bEndpointsChanged = false;
	if (ACannon* Cannon = SourceCannon.Get())
	{
		const FVector NewStart = Cannon->GetProjectileMuzzleTransform().GetLocation();
		AShip* CurrentTarget = TargetShip.Get();
		const FVector TargetCenter = CurrentTarget && CurrentTarget->BuoyancyRoot
			? CurrentTarget->BuoyancyRoot->GetComponentLocation()
			: (CurrentTarget ? CurrentTarget->GetActorLocation() : NewStart);
		const FVector NewDirection = bAimTargetLocked
			? (LockedTargetPoint - NewStart).GetSafeNormal()
			: (TargetCenter - NewStart).GetSafeNormal();
		if (!FVector(LineStart).Equals(NewStart, 0.5f))
		{
			LineStart = NewStart;
			bEndpointsChanged = true;
		}
		if (!NewDirection.IsNearlyZero())
		{
			FixedDirection = NewDirection;
		}
	}
	const FVector NewEnd = ResolveClippedLineEnd(
		FVector(LineStart), FixedDirection, TargetShip.Get(), MaximumDistance);
	if (!FVector(LineEnd).Equals(NewEnd, 0.5f))
	{
		LineEnd = NewEnd;
		bEndpointsChanged = true;
	}
	if (bEndpointsChanged)
	{
		RefreshLineVisual();
		RefreshChargeEffect();
		ForceNetUpdate();
	}
}

void AEnemyShipTimeStopAimLine::RefreshLineVisual()
{
	if (!LineMesh)
	{
		return;
	}
	if (!bWarningLineVisible)
	{
		LineMesh->SetVisibility(false);
		return;
	}
	const FVector Delta = FVector(LineEnd) - FVector(LineStart);
	const float Length = Delta.Size();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		LineMesh->SetVisibility(false);
		return;
	}
	FixedDirection = Delta.GetSafeNormal();

	LineMesh->SetVisibility(true);
	if (LaserMaterial && LineMesh->GetMaterial(0) != LaserMaterial)
	{
		LineMesh->SetMaterial(0, LaserMaterial);
	}
	SetActorLocationAndRotation(
		(FVector(LineStart) + FVector(LineEnd)) * 0.5f,
		Delta.Rotation());
	LineMesh->SetRelativeScale3D(FVector(
		FMath::Max(1.0f, LineThickness) / 100.0f,
		FMath::Max(1.0f, LineThickness) / 100.0f,
		Length / 100.0f));
	RefreshChargeEffect();
}

void AEnemyShipTimeStopAimLine::RefreshChargeEffect()
{
	if (!ChargeEffectComponent)
	{
		return;
	}
	if (!bChargeEffectActive || !ChargeEffect)
	{
		ChargeEffectComponent->Deactivate();
		return;
	}
	if (ChargeEffectComponent->GetAsset() != ChargeEffect)
	{
		ChargeEffectComponent->SetAsset(ChargeEffect);
	}
	ChargeEffectComponent->SetWorldLocationAndRotation(
		FVector(LineStart),
		FixedDirection.Rotation());
	ChargeEffectComponent->SetRelativeScale3D(FVector(FMath::Max(0.01f, ChargeEffectScale)));
	ChargeEffectComponent->SetVariableFloat(TEXT("User.Scale"), FMath::Max(0.01f, ChargeEffectScale));
	if (!ChargeEffectComponent->IsActive())
	{
		ChargeEffectComponent->Activate(true);
	}
}

void AEnemyShipTimeStopAimLine::MulticastPlayInstantHitEffects_Implementation(
	UNiagaraSystem* InTrailEffect,
	UNiagaraSystem* InExplosionEffect,
	FVector_NetQuantize InStart,
	FVector_NetQuantize InEnd,
	bool bHitPlayer,
	float InTrailScale,
	float InTrailLifetimeSeconds,
	float InExplosionScale)
{
	bChargeEffectActive = false;
	RefreshChargeEffect();
	bWarningLineVisible = false;
	if (LineMesh)
	{
		LineMesh->SetVisibility(false);
	}
	if (GetNetMode() == NM_DedicatedServer || !GetWorld())
	{
		return;
	}

	const FVector Start = InStart;
	const FVector End = InEnd;
	const FVector Direction = (End - Start).GetSafeNormal();
	if (InTrailEffect && !Direction.IsNearlyZero())
	{
		UNiagaraComponent* Trail = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), InTrailEffect, Start, Direction.Rotation(), FVector::OneVector, true, false);
		if (Trail)
		{
			const float TrailScale = FMath::Max(0.01f, InTrailScale);
			const float TrailLifetime = FMath::Max(0.01f, InTrailLifetimeSeconds);
			Trail->SetVariableVec3(TEXT("User.Hit"), End);
			Trail->SetVariableFloat(TEXT("User.Elec_Scale"), TrailScale);
			Trail->SetVariableFloat(TEXT("User.Elec_Thickness"), TrailScale);
			Trail->SetVariableFloat(TEXT("User.Elec02_Thickness"), TrailScale);
			Trail->SetVariableFloat(TEXT("User.RibbonWidth"), TrailScale);
			Trail->SetVariableFloat(TEXT("User.Elec_LifeTime"), TrailLifetime);
			Trail->SetVariableFloat(TEXT("User.Elec02_Duration"), TrailLifetime);
			Trail->SetVariableFloat(TEXT("User.RibbonLifeTime"), TrailLifetime);
			Trail->Activate(true);
		}
	}
	if (bHitPlayer && InExplosionEffect)
	{
		USWNiagaraScaleLibrary::SpawnUniformlyScaledSystemAtLocation(
			GetWorld(), InExplosionEffect, End, (-Direction).Rotation(), InExplosionScale, true);
	}
}
