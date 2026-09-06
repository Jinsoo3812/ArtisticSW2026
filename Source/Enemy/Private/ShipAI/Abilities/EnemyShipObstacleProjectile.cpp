#include "ShipAI/Abilities/EnemyShipObstacleProjectile.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ShipAI/Abilities/EnemyShipObstacle.h"
#include "TimerManager.h"
#include "Effects/SWNiagaraScaleLibrary.h"

AEnemyShipObstacleProjectile::AEnemyShipObstacleProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);
	bAlwaysRelevant = true;
	SetMinNetUpdateFrequency(30.0f);

	ProjectileRoot = CreateDefaultSubobject<USphereComponent>(TEXT("ProjectileRoot"));
	ProjectileRoot->InitSphereRadius(15.0f);
	ProjectileRoot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProjectileRoot->SetCollisionResponseToAllChannels(ECR_Ignore);
	ProjectileRoot->SetGenerateOverlapEvents(false);
	SetRootComponent(ProjectileRoot);

	ProjectileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileMesh"));
	ProjectileMesh->SetupAttachment(ProjectileRoot);
	ProjectileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = ProjectileRoot;
	ProjectileMovement->ProjectileGravityScale = 1.0f;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = false;
	ProjectileMovement->bSweepCollision = false;
	ProjectileMovement->bInterpMovement = true;
	ProjectileMovement->bInterpRotation = true;
	ProjectileMovement->InterpLocationTime = 0.05f;
	ProjectileMovement->InterpRotationTime = 0.05f;
	ProjectileMovement->InterpLocationMaxLagDistance = 2000.0f;
	ProjectileMovement->InterpLocationSnapToTargetDistance = 10000.0f;
	ProjectileMovement->SetInterpolatedComponent(ProjectileMesh);
}

void AEnemyShipObstacleProjectile::PostNetReceiveLocationAndRotation()
{
	if (ProjectileMovement
		&& ProjectileMovement->IsActive()
		&& ProjectileMovement->bInterpMovement
		&& ProjectileMovement->GetInterpolatedComponent())
	{
		const FRepMovement& Movement = GetReplicatedMovement();
		const FVector NewLocation = FRepMovement::RebaseOntoLocalOrigin(Movement.Location, this);
		ProjectileMovement->MoveInterpolationTarget(NewLocation, Movement.Rotation);
		return;
	}

	Super::PostNetReceiveLocationAndRotation();
}

void AEnemyShipObstacleProjectile::PostNetReceiveVelocity(const FVector& NewVelocity)
{
	Super::PostNetReceiveVelocity(NewVelocity);
	if (ProjectileMovement && ProjectileMovement->IsActive())
	{
		ProjectileMovement->Velocity = NewVelocity;
		ProjectileMovement->UpdateComponentVelocity();
	}
}

void AEnemyShipObstacleProjectile::InitializeObstacleProjectile(
	const FVector& InLaunchVelocity,
	const FVector& InTargetPoint,
	float InTravelSeconds,
	TSubclassOf<AEnemyShipObstacle> InObstacleClass,
	const FRotator& InObstacleSpawnRotationOffset)
{
	if (!HasAuthority() || !InObstacleClass || InTravelSeconds <= 0.0f)
	{
		Destroy();
		return;
	}

	TargetPoint = InTargetPoint;
	ObstacleClass = InObstacleClass;
	ObstacleSpawnRotationOffset = InObstacleSpawnRotationOffset;
	ProjectileMovement->InitialSpeed = InLaunchVelocity.Size();
	ProjectileMovement->MaxSpeed = FMath::Max(InLaunchVelocity.Size() * 2.0f, 5000.0f);
	ProjectileMovement->Velocity = InLaunchVelocity;
	ProjectileMovement->UpdateComponentVelocity();
	ProjectileMovement->ResetInterpolation();
	GetWorldTimerManager().SetTimer(
		ArrivalTimerHandle,
		this,
		&AEnemyShipObstacleProjectile::ReachTargetAndSpawnObstacle,
		InTravelSeconds,
		false);
	SetLifeSpan(InTravelSeconds + 1.0f);
}

void AEnemyShipObstacleProjectile::ReachTargetAndSpawnObstacle()
{
	if (!HasAuthority() || !GetWorld() || !ObstacleClass)
	{
		Destroy();
		return;
	}

	SetActorLocation(TargetPoint, false, nullptr, ETeleportType::TeleportPhysics);
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = GetOwner();
	SpawnParameters.Instigator = GetInstigator();
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GetWorld()->SpawnActor<AEnemyShipObstacle>(
		ObstacleClass,
		TargetPoint,
		ObstacleSpawnRotationOffset,
		SpawnParameters);
	if (ObstacleSpawnEffect)
	{
		MulticastSpawnObstacleEffect(
			ObstacleSpawnEffect,
			TargetPoint,
			ObstacleSpawnRotationOffset,
			FMath::Max(0.01f, ObstacleSpawnEffectScale),
			FMath::Max(0.01f, ObstacleSpawnEffectLifetimeScale),
			FMath::Max(0.01f, ObstacleSpawnEffectPlaybackSpeed));
	}
	Destroy();
}

void AEnemyShipObstacleProjectile::MulticastSpawnObstacleEffect_Implementation(
	UNiagaraSystem* Effect,
	FVector_NetQuantize Location,
	FRotator Rotation,
	float UniformScale,
	float LifetimeScale,
	float PlaybackSpeed)
{
	if (Effect && GetWorld() && GetNetMode() != NM_DedicatedServer)
	{
		USWNiagaraScaleLibrary::SpawnTunedSystemAtLocation(
			GetWorld(), Effect, Location, Rotation, UniformScale, LifetimeScale, PlaybackSpeed, true);
	}
}
