#include "ShipAI/Abilities/EnemyShipTimeStopProjectile.h"
#include "Room/SWVoyageSpawnLibrary.h"
#include "Room/SWVoyageResetSubsystem.h"

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
#include "Room/SWRoomSnapshotComponent.h"

AEnemyShipTimeStopProjectile::AEnemyShipTimeStopProjectile()
{
	CreateDefaultSubobject<USWRoomSnapshotComponent>(TEXT("RoomSnapshot"));
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
	if (USWVoyageSpawnLibrary::IsActorVoyageGameplayBlocked(this) || bImpactHandled || !HasAuthority())
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
		const FTransform SpawnTransform(FRotator::ZeroRotator, ImpactLocation);
		const USWVoyageResetSubsystem* Voyage = GetWorld()->GetSubsystem<USWVoyageResetSubsystem>();
		if (AEnemyShipTimeStopField* Field = Cast<AEnemyShipTimeStopField>(USWVoyageSpawnLibrary::BeginVoyageActorSpawn(
			this, FieldClass, SpawnTransform, SourceShip, SourceShip, ESpawnActorCollisionHandlingMethod::AlwaysSpawn,
			ESWVoyageActorLifetime::Voyage, Voyage && Voyage->IsActiveVoyageSession() ? Voyage->GetActorGeneration(this) : 0)))
		{
			Field->InitializeTimeStop(EffectRadius, EffectDurationSeconds);
			USWVoyageSpawnLibrary::FinishVoyageActorSpawn(Field, SpawnTransform);
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

void AEnemyShipTimeStopProjectile::CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const
{
	FSWRoomTimeStopProjectileState State;
	if (SourceShip)
		if (const USWRoomSnapshotComponent* Id = SourceShip->FindComponentByClass<USWRoomSnapshotComponent>())
			State.SourceShipId = Id->StableId;
	State.FieldClass = FieldClass ? FSoftClassPath(FieldClass.Get()) : FSoftClassPath();
	State.EffectRadius = EffectRadius;
	State.EffectDurationSeconds = EffectDurationSeconds;
	State.RemainingLife = GetLifeSpan();
	State.GravityScale = ProjectileMovement ? ProjectileMovement->ProjectileGravityScale : 0.f;
	State.bImpactHandled = bImpactHandled;
	FSWRoomDomainPart& Part = OutParts.AddDefaulted_GetRef();
	Part.Domain = ESWRoomDomain::Projectile;
	Part.Version = 1;
	if (!FSWRoomStructCodec::Write(State, Part.Bytes))
	{
		OutParts.Pop();
		FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Domain = TEXT("Projectile");
		Issue.FieldKey = TEXT("TimeStopProjectile");
		Issue.Reason = TEXT("Time-stop projectile serialization failed");
	}
}

bool AEnemyShipTimeStopProjectile::RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError)
{
	FSWRoomTimeStopProjectileState State;
	if (Part.Domain != ESWRoomDomain::Projectile || Part.Version != 1 || !FSWRoomStructCodec::Read(Part.Bytes, State)
		|| !FMath::IsFinite(State.EffectRadius) || State.EffectRadius <= 0.f
		|| !FMath::IsFinite(State.EffectDurationSeconds) || State.EffectDurationSeconds <= 0.f
		|| !FMath::IsFinite(State.RemainingLife) || State.RemainingLife < 0.f
		|| !FMath::IsFinite(State.GravityScale))
	{
		OutError = TEXT("Invalid time-stop projectile state");
		return false;
	}
	if (!State.FieldClass.IsNull())
	{
		FieldClass = State.FieldClass.TryLoadClass<AEnemyShipTimeStopField>();
		if (!FieldClass)
		{
			OutError = TEXT("Time-stop field class missing");
			return false;
		}
	}
	EffectRadius = State.EffectRadius;
	EffectDurationSeconds = State.EffectDurationSeconds;
	bImpactHandled = State.bImpactHandled;
	if (ProjectileMovement) ProjectileMovement->ProjectileGravityScale = State.GravityScale;
	PendingRoomState = State;
	bHasPendingRoomState = true;
	return true;
}

bool AEnemyShipTimeStopProjectile::FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError)
{
	if (!bHasPendingRoomState) return true;
	bHasPendingRoomState = false;
	SourceShip = nullptr;
	if (AActor* const* Found = RegisteredActors.Find(PendingRoomState.SourceShipId)) SourceShip = Cast<AEnemyShip>(*Found);
	if (Collision)
	{
		Collision->SetCollisionObjectType(ECC_GameTraceChannel3);
		Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
		Collision->SetCollisionResponseToChannel(ECC_ShipDamage, ECR_Block);
		if (SourceShip) Collision->IgnoreActorWhenMoving(SourceShip, true);
		Collision->SetCollisionEnabled(bImpactHandled ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
	}
	SetLifeSpan(FMath::Max(KINDA_SMALL_NUMBER, PendingRoomState.RemainingLife));
	return true;
}
