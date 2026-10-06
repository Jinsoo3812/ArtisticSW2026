#include "RangedEnemy/RangedEnemyProjectile.h"

#include "CollisionQueryParams.h"
#include "GameFramework/Pawn.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Item/Projectiles/ProjectileLaunchTypes.h"

namespace
{
	bool IsEnemyBowDebugEnabled()
	{
		const IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("sw.Projectile.DebugLaunch"));
		return Debug && Debug->GetInt() != 0;
	}
}

ARangedEnemyProjectile::ARangedEnemyProjectile()
{
	InitialLifeSpan = 10.0f;
	SetNetUpdateFrequency(30.0f);
	SetMinNetUpdateFrequency(20.0f);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

FCollisionQueryParams ARangedEnemyProjectile::MakeFlightQueryParams() const
{
	FCollisionQueryParams Params = Super::MakeFlightQueryParams();
	// Exclude every component of the firing enemy even before BeginPlay registers ignores.
	// Flight uses explicit world sweeps, so IgnoreActorWhenMoving alone is insufficient.
	if (const APawn* Shooter = GetInstigator()) Params.AddIgnoredActor(Shooter);
	if (const AActor* OwnerActor = GetOwner()) Params.AddIgnoredActor(OwnerActor);
	return Params;
}

bool ARangedEnemyProjectile::LaunchEnemyShot(const FProjectileShotSnapshot& Shot, AActor* Weapon)
{
	if (!HasAuthority() || !ProjectileMovementComp || !CollisionComp || !DirectDamageSpec.IsValid()
		|| Shot.SpawnTransform.ContainsNaN() || Shot.WorldVelocity.ContainsNaN() || Shot.WorldVelocity.IsNearlyZero()) return false;
	IgnoreActorForMovement(GetInstigator());
	IgnoreActorForMovement(GetOwner());
	IgnoreActorForMovement(Weapon);
	const FVector ConstructedLocation = GetActorLocation();
	ProjectileMovementComp->Deactivate();
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	CollisionComp->SetSimulatePhysics(false);
	// Blueprint construction must not move the physical origin away from the sampled socket.
	if (!SetActorTransform(Shot.SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics)) return false;
	if (MeshComp)
	{
		MeshComp->SetSimulatePhysics(false);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (!LaunchShot(Shot)) return false;
	const bool bReady = ProjectileMovementComp->IsActive() && ProjectileMovementComp->bSimulationEnabled
		&& ProjectileMovementComp->UpdatedComponent == CollisionComp
		&& ProjectileMovementComp->Velocity.Equals(Shot.WorldVelocity, 0.01);
	if (!bReady) return false;
	EnemyShotId = Shot.Input.ShotId;
	EnemyLaunchFrame = GFrameCounter;
	LastDebugPosition = GetActorLocation();
	bFirstStepLogged = false;
	SetActorTickEnabled(IsEnemyBowDebugEnabled());
	if (IsEnemyBowDebugEnabled())
	{
		const FVector ToAim = Shot.Input.AimDirection.GetSafeNormal();
		const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(ToAim, ProjectileMovementComp->Velocity.GetSafeNormal()), -1.0, 1.0)));
		UE_LOG(LogTemp, Display, TEXT("[EnemyBowLaunch] Id=%s Arrow=%s Shooter=%s Weapon=%s Muzzle=%s Root=%s ConstructionOffset=%.4f OriginError=%.4f AimAngle=%.4f Gravity=%.3f"),
			*EnemyShotId.ToString(), *GetName(), *GetNameSafe(GetInstigator()), *GetNameSafe(Weapon),
			*Shot.Input.MuzzleTransform.GetLocation().ToString(), *GetActorLocation().ToString(),
			FVector::Distance(ConstructedLocation, Shot.SpawnTransform.GetLocation()),
			FVector::Distance(GetActorLocation(), Shot.Input.MuzzleTransform.GetLocation()), Angle, Shot.Input.GravityZ);
	}
	return true;
}

void ARangedEnemyProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !IsEnemyBowDebugEnabled()) { SetActorTickEnabled(false); return; }
	if (EnemyLaunchFrame == GFrameCounter) return;
	const FVector Position = GetActorLocation();
	DrawDebugLine(GetWorld(), LastDebugPosition, Position, FColor::Green, false, 3.0f, 0, 2.0f);
	if (!bFirstStepLogged)
	{
		bFirstStepLogged = true;
		UE_LOG(LogTemp, Display, TEXT("[EnemyBowFirstStep] Id=%s Start=%s End=%s Velocity=%s Dt=%.6f"),
			*EnemyShotId.ToString(), *LastDebugPosition.ToString(), *Position.ToString(), *GetVelocity().ToString(), DeltaSeconds);
	}
	LastDebugPosition = Position;
}

void ARangedEnemyProjectile::HandleFlightImpact(const FHitResult& Hit)
{
	if (HasAuthority() && IsEnemyBowDebugEnabled())
	{
		const UPrimitiveComponent* Component = Hit.GetComponent();
		DrawDebugLine(GetWorld(), LastDebugPosition, GetActorLocation(), FColor::Green, false, 3.0f, 0, 2.0f);
		DrawDebugPoint(GetWorld(), Hit.ImpactPoint, 14.0f, FColor::Red, false, 3.0f);
		UE_LOG(LogTemp, Display, TEXT("[EnemyBowHit] Id=%s Arrow=%s Actor=%s Component=%s Profile=%s Time=%.6f StartPenetrating=%d Root=%s Impact=%s"),
			*EnemyShotId.ToString(), *GetName(), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Component),
			Component ? *Component->GetCollisionProfileName().ToString() : TEXT("None"), Hit.Time,
			Hit.bStartPenetrating, *GetActorLocation().ToString(), *Hit.ImpactPoint.ToString());
	}
	Super::HandleFlightImpact(Hit);
	SetActorTickEnabled(false);
}
