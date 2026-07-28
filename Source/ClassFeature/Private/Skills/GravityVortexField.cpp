#include "Skills/GravityVortexField.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Ship.h"

AGravityVortexField::AGravityVortexField()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

void AGravityVortexField::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		SourceId = FGuid::NewGuid();
		ActivationServerTime = GetSynchronizedServerTime();
		ExpireServerTime = ActivationServerTime + FMath::Max(0.05f, Duration);
		SetLifeSpan(FMath::Max(0.05f, Duration));
		RefreshTargets();
	}
}

void AGravityVortexField::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		RefreshAccumulator += DeltaSeconds;
		if (RefreshAccumulator >= FMath::Max(0.01f, TargetRefreshInterval))
		{
			RefreshAccumulator = 0.0f;
			RefreshTargets();
		}
	}

#if !UE_SERVER
	if (bDrawDebug && GetWorld())
	{
		const FVector DebugCenter = GetActorLocation() + FVector::UpVector * DebugDrawHeightOffset;
		const float HalfDebugHeight = 20.0f;
		const int32 Segments = FMath::Clamp(DebugCircleSegments, 16, 256);
		const float Thickness = FMath::Max(0.5f, DebugLineThickness);

		// A shallow cylinder is substantially more visible than a single circle on
		// translucent water and remains readable from an oblique gameplay camera.
		DrawDebugCylinder(
			GetWorld(),
			DebugCenter - FVector::UpVector * HalfDebugHeight,
			DebugCenter + FVector::UpVector * HalfDebugHeight,
			PullRadius,
			Segments,
			FColor::Emerald,
			false,
			0.0f,
			1,
			Thickness);

		DrawDebugCircle(
			GetWorld(), DebugCenter, FMath::Max(InnerDeadZoneRadius, 25.0f), 32,
			FColor::Purple, false, 0.0f, 1, Thickness * 0.65f,
			FVector::ForwardVector, FVector::RightVector, false);
		DrawDebugLine(
			GetWorld(), DebugCenter, DebugCenter + FVector::UpVector * 400.0f,
			FColor::Yellow, false, 0.0f, 1, Thickness);
	}
#endif
}

void AGravityVortexField::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		for (const TWeakObjectPtr<AShip>& ShipPtr : AffectedShips)
		{
			ReleaseShip(ShipPtr.Get());
		}
		AffectedShips.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

void AGravityVortexField::RefreshTargets()
{
	if (!HasAuthority() || !GetWorld() || !SourceId.IsValid())
	{
		return;
	}

	TSet<TWeakObjectPtr<AShip>> NewAffectedShips;
	const float RadiusSq = FMath::Square(FMath::Max(PullRadius, 1.0f));

	for (TActorIterator<AShip> It(GetWorld()); It; ++It)
	{
		AShip* Ship = *It;
		if (!IsValid(Ship) || !Ship->IsEnemyShipForEffects() || !IsValid(Ship->BuoyancyRoot))
		{
			continue;
		}

		FVector Delta = GetActorLocation() - Ship->GetActorLocation();
		Delta.Z = 0.0f;
		if (Delta.SizeSquared() > RadiusSq)
		{
			continue;
		}

		NewAffectedShips.Add(Ship);
		Ship->SetExternalAccelerationSource(SourceId, CalculateAcceleration(Ship));
		if (bSuppressEnemyPropulsion)
		{
			Ship->AddPropulsionSuppression(SourceId);
		}
	}

	for (const TWeakObjectPtr<AShip>& PreviousShip : AffectedShips)
	{
		if (!NewAffectedShips.Contains(PreviousShip))
		{
			ReleaseShip(PreviousShip.Get());
		}
	}

	AffectedShips = MoveTemp(NewAffectedShips);
}

void AGravityVortexField::ReleaseShip(AShip* Ship)
{
	if (!IsValid(Ship) || !SourceId.IsValid())
	{
		return;
	}

	Ship->RemoveExternalAccelerationSource(SourceId);
	Ship->RemovePropulsionSuppression(SourceId);
}

FVector AGravityVortexField::CalculateAcceleration(AShip* Ship) const
{
	if (!IsValid(Ship))
	{
		return FVector::ZeroVector;
	}

	FVector ToCenter = GetActorLocation() - Ship->GetActorLocation();
	ToCenter.Z = 0.0f;
	const float Distance = ToCenter.Size();
	if (Distance <= FMath::Max(0.0f, InnerDeadZoneRadius) || Distance > PullRadius)
	{
		return FVector::ZeroVector;
	}

	const float DistanceAlpha = FMath::Clamp(Distance / FMath::Max(PullRadius, 1.0f), 0.0f, 1.0f);
	const float Falloff = PullFalloffCurve
		? FMath::Max(0.0f, PullFalloffCurve->GetFloatValue(DistanceAlpha))
		: FMath::Lerp(1.0f, FMath::Clamp(EdgePullStrengthRatio, 0.0f, 1.0f), DistanceAlpha);

	const FVector PullDirection = ToCenter / Distance;
	FVector Acceleration = PullDirection * FMath::Max(0.0f, PullAcceleration) * Falloff;

	if (Ship->BuoyancyRoot && RadialDamping > 0.0f)
	{
		const float InwardSpeed = FVector::DotProduct(Ship->BuoyancyRoot->GetPhysicsLinearVelocity(), PullDirection);
		if (InwardSpeed > MaxInwardSpeed)
		{
			Acceleration -= PullDirection * (InwardSpeed - MaxInwardSpeed) * RadialDamping;
		}
	}

	Acceleration.Z = 0.0f;
	return Acceleration.GetClampedToMaxSize(FMath::Max(0.0f, MaxPullAcceleration));
}

double AGravityVortexField::GetSynchronizedServerTime() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			return GameState->GetServerWorldTimeSeconds();
		}
		return World->GetTimeSeconds();
	}
	return 0.0;
}

void AGravityVortexField::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGravityVortexField, ActivationServerTime);
	DOREPLIFETIME(AGravityVortexField, ExpireServerTime);
}
