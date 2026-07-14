#include "Item/Components/BowComponent.h"

#include "CollisionChannels.h"
#include "DrawDebugHelpers.h"
#include "Item/Weapons/BowItem.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

UBowComponent::UBowComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UBowComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UBowComponent, bIsAiming);
	DOREPLIFETIME(UBowComponent, DrawAlpha);
}

void UBowComponent::SetAiming(bool bNewAiming)
{
	if (bIsAiming == bNewAiming)
	{
		return;
	}

	bIsAiming = bNewAiming;
	OnAimStateChanged.Broadcast(bIsAiming);

	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		ServerSetAiming(bNewAiming);
	}
}

void UBowComponent::SetDrawAlpha(float NewDrawAlpha)
{
	const float ClampedDrawAlpha = FMath::Clamp(NewDrawAlpha, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(DrawAlpha, ClampedDrawAlpha))
	{
		return;
	}

	DrawAlpha = ClampedDrawAlpha;
	OnDrawAlphaChanged.Broadcast(DrawAlpha);

	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		ServerSetDrawAlpha(DrawAlpha);
	}
}

float UBowComponent::GetCurrentFireSpeed() const
{
	return FMath::Lerp(MinFireSpeed, MaxFireSpeed, DrawAlpha);
}

FTransform UBowComponent::BuildArrowSpawnTransform() const
{
	if (const ABowItem* Bow = GetOwningBow())
	{
		return Bow->GetArrowSpawnTransform();
	}

	return GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity;
}

bool UBowComponent::CalculateAim(const FVector& ViewLocation, const FVector& ViewForward, const TArray<AActor*>& ActorsToIgnore, FBowAimResult& OutAimResult) const
{
	const FVector AimDirection = ViewForward.GetSafeNormal();
	if (AimDirection.IsNearlyZero())
	{
		return false;
	}

	OutAimResult.CameraTraceStart = ViewLocation;
	OutAimResult.CameraTraceEnd = ViewLocation + AimDirection * AimTraceDistance;
	OutAimResult.CameraAimTarget = OutAimResult.CameraTraceEnd;
	OutAimResult.SocketTraceStart = BuildArrowSpawnTransform().GetLocation();
	OutAimResult.SocketTraceEnd = OutAimResult.CameraAimTarget;
	OutAimResult.TraceStart = OutAimResult.SocketTraceStart;
	OutAimResult.TraceEnd = OutAimResult.SocketTraceEnd;
	OutAimResult.AimTarget = OutAimResult.CameraAimTarget;
	OutAimResult.bBlockingHit = false;
	OutAimResult.bCameraBlockingHit = false;
	OutAimResult.bSocketBlockingHit = false;
	OutAimResult.HitActor = nullptr;

	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}

	FHitResult HitResult;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BowAimTrace), false);
	for (AActor* ActorToIgnore : ActorsToIgnore)
	{
		if (ActorToIgnore)
		{
			QueryParams.AddIgnoredActor(ActorToIgnore);
		}
	}

	if (World->LineTraceSingleByChannel(HitResult, OutAimResult.CameraTraceStart, OutAimResult.CameraTraceEnd, ECC_WeaponAim, QueryParams))
	{
		OutAimResult.CameraAimTarget = HitResult.ImpactPoint;
		OutAimResult.bCameraBlockingHit = true;
		OutAimResult.HitActor = HitResult.GetActor();
	}

	OutAimResult.SocketTraceEnd = OutAimResult.CameraAimTarget;
	OutAimResult.TraceEnd = OutAimResult.SocketTraceEnd;

	FHitResult SocketHitResult;
	if (ResolveAimTargetFromSocket(OutAimResult.SocketTraceStart, OutAimResult.CameraAimTarget, ActorsToIgnore, OutAimResult.AimTarget, &SocketHitResult))
	{
		OutAimResult.bSocketBlockingHit = SocketHitResult.bBlockingHit;
		if (OutAimResult.bSocketBlockingHit)
		{
			OutAimResult.HitActor = SocketHitResult.GetActor();
		}
	}

	OutAimResult.bBlockingHit = OutAimResult.bCameraBlockingHit || OutAimResult.bSocketBlockingHit;
	return true;
}

bool UBowComponent::ResolveAimTargetFromSocket(const FVector& SocketLocation, const FVector& CandidateAimTarget, const TArray<AActor*>& ActorsToIgnore, FVector& OutAimTarget, FHitResult* OutHitResult) const
{
	OutAimTarget = CandidateAimTarget;
	if (OutHitResult)
	{
		*OutHitResult = FHitResult();
		OutHitResult->TraceStart = SocketLocation;
		OutHitResult->TraceEnd = CandidateAimTarget;
	}

	if (FVector::DistSquared(SocketLocation, CandidateAimTarget) <= FMath::Square(10.0f))
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BowSocketAimTrace), false);
	for (AActor* ActorToIgnore : ActorsToIgnore)
	{
		if (ActorToIgnore)
		{
			QueryParams.AddIgnoredActor(ActorToIgnore);
		}
	}

	FHitResult HitResult;
	if (World->LineTraceSingleByChannel(HitResult, SocketLocation, CandidateAimTarget, ECC_WeaponAim, QueryParams))
	{
		OutAimTarget = HitResult.ImpactPoint;
	}
	else
	{
		HitResult.TraceStart = SocketLocation;
		HitResult.TraceEnd = CandidateAimTarget;
	}

	if (OutHitResult)
	{
		*OutHitResult = HitResult;
	}

	return true;
}

bool UBowComponent::TryCalculateLaunchVelocity(const FVector& SpawnLocation, const FVector& AimTarget, const TArray<AActor*>& ActorsToIgnore, FVector& OutLaunchVelocity) const
{
	OutLaunchVelocity = FVector::ZeroVector;

	UWorld* World = GetWorld();
	const float FireSpeed = GetCurrentFireSpeed();
	if (!World || FireSpeed <= 0.0f)
	{
		return false;
	}

	if (FVector::DistSquared(SpawnLocation, AimTarget) <= FMath::Square(10.0f))
	{
		return false;
	}

	if (ProjectileGravityScaleForAim <= KINDA_SMALL_NUMBER)
	{
		OutLaunchVelocity = (AimTarget - SpawnLocation).GetSafeNormal() * FireSpeed;
		return !OutLaunchVelocity.IsNearlyZero();
	}

	FCollisionResponseParams ResponseParams = FCollisionResponseParams::DefaultResponseParam;
	TArray<AActor*> IgnoreActors = ActorsToIgnore;
	if (ABowItem* Bow = GetOwningBow())
	{
		IgnoreActors.AddUnique(Bow);
	}

	const float OverrideGravityZ = World->GetGravityZ() * ProjectileGravityScaleForAim;
	UGameplayStatics::FSuggestProjectileVelocityParameters ProjectileParams(
		World,
		SpawnLocation,
		AimTarget,
		FireSpeed);

	ProjectileParams.bFavorHighArc = false;
	ProjectileParams.CollisionRadius = 0.0f;
	ProjectileParams.OverrideGravityZ = OverrideGravityZ;
	ProjectileParams.TraceOption = ESuggestProjVelocityTraceOption::DoNotTrace;
	ProjectileParams.ResponseParam = ResponseParams;
	ProjectileParams.ActorsToIgnore = IgnoreActors;
	ProjectileParams.bDrawDebug = false;
	ProjectileParams.bAcceptClosestOnNoSolutions = false;

	return UGameplayStatics::SuggestProjectileVelocity(ProjectileParams, OutLaunchVelocity);
}

void UBowComponent::DrawServerFireDebug(const FVector& SpawnLocation, const FVector& AimTarget) const
{
	if (!bDrawServerFireDebug)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		DrawDebugLine(World, SpawnLocation, AimTarget, FColor::Cyan, false, FireDebugDrawDuration, 0, 2.0f);
	}
}

void UBowComponent::ServerSetAiming_Implementation(bool bNewAiming)
{
	SetAiming(bNewAiming);
}

void UBowComponent::ServerSetDrawAlpha_Implementation(float NewDrawAlpha)
{
	SetDrawAlpha(NewDrawAlpha);
}

void UBowComponent::OnRep_IsAiming()
{
	OnAimStateChanged.Broadcast(bIsAiming);
}

void UBowComponent::OnRep_DrawAlpha()
{
	OnDrawAlphaChanged.Broadcast(DrawAlpha);
}

ABowItem* UBowComponent::GetOwningBow() const
{
	return Cast<ABowItem>(GetOwner());
}
