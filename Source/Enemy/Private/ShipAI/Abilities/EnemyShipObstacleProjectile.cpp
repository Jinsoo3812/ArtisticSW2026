#include "ShipAI/Abilities/EnemyShipObstacleProjectile.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "ShipAI/Abilities/EnemyShipObstacle.h"
#include "TimerManager.h"

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
	ProjectileMovement->SetInterpolatedComponent(ProjectileMesh);
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
	AEnemyShipObstacle* SpawnedObstacle = GetWorld()->SpawnActor<AEnemyShipObstacle>(
		ObstacleClass,
		TargetPoint,
		ObstacleSpawnRotationOffset,
		SpawnParameters);

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[EnemyShipObstacle] Target reached; obstacle spawned. Projectile=%s Target=%s Rotation=%s Obstacle=%s"),
		*GetName(),
		*TargetPoint.ToCompactString(),
		*ObstacleSpawnRotationOffset.ToCompactString(),
		*GetNameSafe(SpawnedObstacle));
	Destroy();
}
