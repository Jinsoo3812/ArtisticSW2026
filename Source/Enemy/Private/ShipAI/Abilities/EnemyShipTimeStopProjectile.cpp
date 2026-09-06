#include "ShipAI/Abilities/EnemyShipTimeStopProjectile.h"

#include "CollisionChannels.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Effects/SWNiagaraScaleLibrary.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"
#include "Ship.h"
#include "ShipAI/Abilities/EnemyShipTimeStopField.h"
#include "ShipAI/EnemyShip.h"

AEnemyShipTimeStopProjectile::AEnemyShipTimeStopProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(30.0f);
	Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Collision->SetGenerateOverlapEvents(false);
	Collision->SetNotifyRigidBodyCollision(true);
	Collision->OnComponentHit.AddUniqueDynamic(this, &AEnemyShipTimeStopProjectile::OnProjectileHit);
	SetRootComponent(Collision);

	ProjectileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileMesh"));
	ProjectileMesh->SetupAttachment(Collision);
	ProjectileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshFinder(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMeshFinder.Succeeded())
	{
		ProjectileMesh->SetStaticMesh(SphereMeshFinder.Object);
		ProjectileMesh->SetRelativeScale3D(FVector(0.5f));
	}

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = Collision;
	ProjectileMovement->bSweepCollision = true;
	ProjectileMovement->bShouldBounce = false;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->ProjectileGravityScale = 0.0f;
	ProjectileMovement->InitialSpeed = 5000.0f;
	ProjectileMovement->MaxSpeed = 5000.0f;

	ProjectileEffectComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ProjectileEffectComponent"));
	ProjectileEffectComponent->SetupAttachment(Collision);
	ProjectileEffectComponent->SetAutoActivate(false);
}

void AEnemyShipTimeStopProjectile::BeginPlay()
{
	Super::BeginPlay();
	if (ProjectileEffectComponent && ProjectileEffect && GetNetMode() != NM_DedicatedServer)
	{
		ProjectileEffectComponent->SetAsset(ProjectileEffect);
		USWNiagaraScaleLibrary::ApplyEffectTuning(
			ProjectileEffectComponent, ProjectileEffectScale,
			ProjectileEffectLifetimeScale, ProjectileEffectPlaybackSpeed);
		ProjectileEffectComponent->Activate(true);
	}
}

void AEnemyShipTimeStopProjectile::InitializeTimeStopProjectile(
	AEnemyShip* InSourceShip,
	const FVector& LaunchDirection,
	float Speed,
	float InLifetimeSeconds,
	float InEffectRadius,
	float InEffectDurationSeconds,
	TSubclassOf<AEnemyShipTimeStopField> InFieldClass)
{
	SourceShip = InSourceShip;
	FieldClass = InFieldClass;
	EffectRadius = FMath::Max(1.0f, InEffectRadius);
	EffectDurationSeconds = FMath::Max(0.05f, InEffectDurationSeconds);
	const FVector Direction = LaunchDirection.GetSafeNormal();
	const float ResolvedSpeed = FMath::Max(1.0f, Speed);

	Collision->SetCollisionObjectType(ECC_GameTraceChannel3);
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_ShipDamage, ECR_Block);
	if (SourceShip)
	{
		Collision->IgnoreActorWhenMoving(SourceShip, true);
	}
	if (AActor* OwnerActor = GetOwner())
	{
		Collision->IgnoreActorWhenMoving(OwnerActor, true);
	}
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	ProjectileMovement->InitialSpeed = ResolvedSpeed;
	ProjectileMovement->MaxSpeed = ResolvedSpeed;
	ProjectileMovement->Velocity = Direction * ResolvedSpeed;
	ProjectileMovement->UpdateComponentVelocity();
	SetLifeSpan(FMath::Max(0.05f, InLifetimeSeconds));
}

void AEnemyShipTimeStopProjectile::OnProjectileHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (bImpactHandled || !HasAuthority())
	{
		return;
	}
	AShip* HitShip = Cast<AShip>(OtherActor);
	if (!HitShip || HitShip->IsEnemyShipForEffects()
		|| !HitShip->ActorHasTag(TEXT("Player")) || HitShip->ActorHasTag(TEXT("Enemy")))
	{
		return;
	}
	bImpactHandled = true;
	const FVector ImpactLocation = Hit.ImpactPoint.IsNearlyZero()
		? GetActorLocation()
		: FVector(Hit.ImpactPoint);
	if (ExplosionEffect)
	{
		const FVector TravelDirection = GetVelocity().GetSafeNormal();
		const FRotator EffectRotation = TravelDirection.IsNearlyZero()
			? GetActorRotation()
			: (-TravelDirection).Rotation();
		MulticastSpawnExplosionEffect(
			ExplosionEffect,
			ImpactLocation,
			EffectRotation,
			FMath::Max(0.01f, ExplosionEffectScale),
			FMath::Max(0.01f, ExplosionEffectLifetimeScale),
			FMath::Max(0.01f, ExplosionEffectPlaybackSpeed));
	}

	if (FieldClass && GetWorld())
	{
		FActorSpawnParameters Params;
		Params.Owner = SourceShip;
		Params.Instigator = SourceShip;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AEnemyShipTimeStopField* Field = GetWorld()->SpawnActor<AEnemyShipTimeStopField>(
			FieldClass, ImpactLocation, FRotator::ZeroRotator, Params))
		{
			Field->InitializeTimeStop(EffectRadius, EffectDurationSeconds);
		}
	}
	Destroy();
}

void AEnemyShipTimeStopProjectile::MulticastSpawnExplosionEffect_Implementation(
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
			GetWorld(), Effect, Location, Rotation, UniformScale,
			LifetimeScale, PlaybackSpeed, true);
	}
}
