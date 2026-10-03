#include "ShipAI/Abilities/EnemyShipObstacleProjectile.h"

#include "Components/SphereComponent.h"
#include "Item/Projectiles/ProjectileLaunchInitialization.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ShipAI/Abilities/EnemyShipObstacle.h"
#include "TimerManager.h"
#include "Effects/SWNiagaraScaleLibrary.h"
#include "Room/SWRoomSnapshotComponent.h"

AEnemyShipObstacleProjectile::AEnemyShipObstacleProjectile()
{
	CreateDefaultSubobject<USWRoomSnapshotComponent>(TEXT("RoomSnapshot"));
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
	if (!ProjectileLaunchInitialization::ApplyWorldVelocity(ProjectileMovement, InLaunchVelocity,
		FMath::Max(InLaunchVelocity.Size() * 2.0f, 5000.0f)))
	{
		Destroy();
		return;
	}
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
	if (bArrivalHandled) return;
	bArrivalHandled = true;
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

void AEnemyShipObstacleProjectile::CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const
{
	FSWRoomObstacleProjectileState State;
	State.TargetPoint = TargetPoint;
	State.RotationOffset = ObstacleSpawnRotationOffset;
	State.ObstacleClass = ObstacleClass ? FSoftClassPath(ObstacleClass.Get()) : FSoftClassPath();
	State.ArrivalRemaining = GetWorldTimerManager().IsTimerActive(ArrivalTimerHandle)
		? FMath::Max(0.f, GetWorldTimerManager().GetTimerRemaining(ArrivalTimerHandle)) : 0.f;
	State.RemainingLife = GetLifeSpan();
	State.GravityScale = ProjectileMovement ? ProjectileMovement->ProjectileGravityScale : 1.f;
	State.bArrivalHandled = bArrivalHandled;
	FSWRoomDomainPart& Part = OutParts.AddDefaulted_GetRef();
	Part.Domain = ESWRoomDomain::Projectile;
	Part.Version = 1;
	if (!FSWRoomStructCodec::Write(State, Part.Bytes))
	{
		OutParts.Pop();
		FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Domain = TEXT("Projectile");
		Issue.FieldKey = TEXT("ObstacleCarrier");
		Issue.Reason = TEXT("Obstacle carrier serialization failed");
	}
}

bool AEnemyShipObstacleProjectile::RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError)
{
	FSWRoomObstacleProjectileState State;
	if (Part.Domain != ESWRoomDomain::Projectile || Part.Version != 1 || !FSWRoomStructCodec::Read(Part.Bytes, State)
		|| State.TargetPoint.ContainsNaN() || State.RotationOffset.ContainsNaN()
		|| !FMath::IsFinite(State.ArrivalRemaining) || State.ArrivalRemaining < 0.f
		|| !FMath::IsFinite(State.RemainingLife) || State.RemainingLife < 0.f
		|| !FMath::IsFinite(State.GravityScale))
	{
		OutError = TEXT("Invalid obstacle carrier state");
		return false;
	}
	ObstacleClass = State.ObstacleClass.IsNull() ? nullptr : State.ObstacleClass.TryLoadClass<AEnemyShipObstacle>();
	if (!State.ObstacleClass.IsNull() && !ObstacleClass)
	{
		OutError = TEXT("Obstacle class missing");
		return false;
	}
	TargetPoint = State.TargetPoint;
	ObstacleSpawnRotationOffset = State.RotationOffset;
	bArrivalHandled = State.bArrivalHandled;
	if (ProjectileMovement) ProjectileMovement->ProjectileGravityScale = State.GravityScale;
	GetWorldTimerManager().ClearTimer(ArrivalTimerHandle);
	PendingRoomState = State;
	bHasPendingRoomState = true;
	return true;
}

bool AEnemyShipObstacleProjectile::FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError)
{
	if (!bHasPendingRoomState) return true;
	bHasPendingRoomState = false;
	if (!bArrivalHandled)
	{
		if (PendingRoomState.ArrivalRemaining <= 0.f)
			ArrivalTimerHandle = GetWorldTimerManager().SetTimerForNextTick(this, &AEnemyShipObstacleProjectile::ReachTargetAndSpawnObstacle);
		else
			GetWorldTimerManager().SetTimer(ArrivalTimerHandle, this, &AEnemyShipObstacleProjectile::ReachTargetAndSpawnObstacle,
				PendingRoomState.ArrivalRemaining, false);
	}
	SetLifeSpan(FMath::Max(KINDA_SMALL_NUMBER, PendingRoomState.RemainingLife));
	return true;
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
