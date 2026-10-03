#include "Projectiles/GravityVortexProjectile.h"

#include "Components/SphereComponent.h"
#include "Item/Projectiles/ProjectileLaunchInitialization.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Effects/SWNiagaraScaleLibrary.h"
#include "Skills/GravityVortexField.h"
#include "WaterSurfaceQueryLibrary.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomSnapshotSubsystem.h"

AGravityVortexProjectile::AGravityVortexProjectile()
{
	CreateDefaultSubobject<USWRoomSnapshotComponent>(TEXT("RoomSnapshot"));
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	SetRootComponent(CollisionSphere);
	CollisionSphere->InitSphereRadius(18.0f);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CollisionSphere->SetGenerateOverlapEvents(false);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(CollisionSphere);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetGenerateOverlapEvents(false);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionSphere;
	ProjectileMovement->InitialSpeed = 2200.0f;
	ProjectileMovement->MaxSpeed = 2200.0f;
	ProjectileMovement->ProjectileGravityScale = 1.0f;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bAutoActivate = false;
	ProjectileMovement->bInterpMovement = true;
	ProjectileMovement->bInterpRotation = true;
	ProjectileMovement->InterpLocationTime = 0.05f;
	ProjectileMovement->InterpRotationTime = 0.05f;
	ProjectileMovement->InterpLocationMaxLagDistance = 2000.0f;
	ProjectileMovement->InterpLocationSnapToTargetDistance = 10000.0f;
	ProjectileMovement->SetInterpolatedComponent(VisualMesh);

	ProjectileEffectComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ProjectileEffect"));
	ProjectileEffectComponent->SetupAttachment(VisualMesh);
	ProjectileEffectComponent->SetAutoActivate(false);

	FieldClass = AGravityVortexField::StaticClass();
}

void AGravityVortexProjectile::PostNetReceiveLocationAndRotation()
{
	if (ProjectileMovement && ProjectileMovement->IsActive()
		&& ProjectileMovement->bInterpMovement && ProjectileMovement->GetInterpolatedComponent())
	{
		const FRepMovement& Movement = GetReplicatedMovement();
		ProjectileMovement->MoveInterpolationTarget(
			FRepMovement::RebaseOntoLocalOrigin(Movement.Location, this), Movement.Rotation);
		return;
	}
	Super::PostNetReceiveLocationAndRotation();
}

void AGravityVortexProjectile::PostNetReceiveVelocity(const FVector& NewVelocity)
{
	Super::PostNetReceiveVelocity(NewVelocity);
	if (ProjectileMovement)
	{
		ProjectileMovement->Velocity = NewVelocity;
		ProjectileMovement->UpdateComponentVelocity();
		if (!ProjectileMovement->IsActive()) ProjectileMovement->Activate(true);
	}
}

void AGravityVortexProjectile::BeginPlay()
{
	Super::BeginPlay();
	PreviousLocation = GetActorLocation();
	if (VisualMesh)
	{
		if (ProjectileMesh)
		{
			VisualMesh->SetStaticMesh(ProjectileMesh);
		}
		VisualMesh->SetRelativeScale3D(FVector(FMath::Max(0.001f, ProjectileMeshScale)));
	}
	if (ProjectileEffectComponent && ProjectileEffect && GetNetMode() != NM_DedicatedServer)
	{
		ProjectileEffectComponent->SetAsset(ProjectileEffect);
		USWNiagaraScaleLibrary::ApplyEffectTuning(
			ProjectileEffectComponent,
			ProjectileEffectScale,
			ProjectileEffectLifetimeScale,
			ProjectileEffectPlaybackSpeed);
		ProjectileEffectComponent->Activate(true);
	}

	if (HasAuthority())
	{
		if (!GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()->IsRestoringSnapshot())
			SetLifeSpan(FMath::Max(0.1f, MaxProjectileLifetime));
	}
}

void AGravityVortexProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

#if !UE_SERVER
	if (bDrawDebug && GetWorld())
	{
		DrawDebugSphere(GetWorld(), GetActorLocation(), 24.0f, 8, FColor::Yellow, false, 0.0f, 0, 2.0f);
	}
#endif

	if (!HasAuthority() || bActivated || GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()->IsRestoringSnapshot())
	{
		return;
	}

	const FVector CurrentLocation = GetActorLocation();
	float PreviousWaterZ = 0.0f;
	float CurrentWaterZ = 0.0f;
	const bool bHadPreviousWater = QueryWaterSurfaceAtLocation(PreviousLocation, PreviousWaterZ);
	const bool bHasCurrentWater = QueryWaterSurfaceAtLocation(CurrentLocation, CurrentWaterZ);

	if (bHasCurrentWater)
	{
		const float CurrentSignedHeight = CurrentLocation.Z - CurrentWaterZ;
		if (bHadPreviousWater)
		{
			const float PreviousSignedHeight = PreviousLocation.Z - PreviousWaterZ;
			if (PreviousSignedHeight > 0.0f && CurrentSignedHeight <= 0.0f)
			{
				const float Denominator = PreviousSignedHeight - CurrentSignedHeight;
				const float Alpha = Denominator > UE_SMALL_NUMBER
					? FMath::Clamp(PreviousSignedHeight / Denominator, 0.0f, 1.0f)
					: 1.0f;
				FVector SurfaceLocation = FMath::Lerp(PreviousLocation, CurrentLocation, Alpha);
				SurfaceLocation.Z = FMath::Lerp(PreviousWaterZ, CurrentWaterZ, Alpha);
				ActivateAtWaterSurface(SurfaceLocation);
				return;
			}
		}
		else if (CurrentSignedHeight <= 0.0f)
		{
			FVector SurfaceLocation = CurrentLocation;
			SurfaceLocation.Z = CurrentWaterZ;
			ActivateAtWaterSurface(SurfaceLocation);
			return;
		}
	}

	PreviousLocation = CurrentLocation;
}

void AGravityVortexProjectile::CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const
{
	FSWRoomGravityProjectileState State;
	State.PreviousLocation = PreviousLocation;
	State.bActivated = bActivated;
	State.bIncludeWaveHeight = bIncludeWaveHeight;
	State.RemainingLife = GetLifeSpan();
	State.GravityScale = ProjectileMovement ? ProjectileMovement->ProjectileGravityScale : 1.f;
	State.FieldClass = FieldClass ? FSoftClassPath(FieldClass.Get()) : FSoftClassPath();
	FSWRoomDomainPart& Part = OutParts.AddDefaulted_GetRef();
	Part.Domain = ESWRoomDomain::Projectile;
	Part.Version = 1;
	if (!FSWRoomStructCodec::Write(State, Part.Bytes))
	{
		OutParts.Pop();
		FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Domain = TEXT("Projectile");
		Issue.FieldKey = TEXT("GravityVortexState");
		Issue.Reason = TEXT("Gravity vortex projectile serialization failed");
	}
}

bool AGravityVortexProjectile::RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError)
{
	FSWRoomGravityProjectileState State;
	if (Part.Domain != ESWRoomDomain::Projectile || Part.Version != 1 || !FSWRoomStructCodec::Read(Part.Bytes, State)
		|| State.PreviousLocation.ContainsNaN() || !FMath::IsFinite(State.RemainingLife) || State.RemainingLife < 0.f
		|| !FMath::IsFinite(State.GravityScale))
	{
		OutError = TEXT("Invalid gravity vortex projectile state");
		return false;
	}
	if (!State.FieldClass.IsNull())
	{
		FieldClass = State.FieldClass.TryLoadClass<AGravityVortexField>();
		if (!FieldClass)
		{
			OutError = TEXT("Gravity vortex field class missing");
			return false;
		}
	}
	PreviousLocation = State.PreviousLocation;
	bActivated = State.bActivated;
	bIncludeWaveHeight = State.bIncludeWaveHeight;
	if (ProjectileMovement) ProjectileMovement->ProjectileGravityScale = State.GravityScale;
	PendingRoomState = State;
	bHasPendingRoomState = true;
	return true;
}

bool AGravityVortexProjectile::FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError)
{
	if (!bHasPendingRoomState) return true;
	bHasPendingRoomState = false;
	SetLifeSpan(FMath::Max(KINDA_SMALL_NUMBER, PendingRoomState.RemainingLife));
	return true;
}

void AGravityVortexProjectile::LaunchProjectile(const FVector& LaunchVelocity)
{
	if (ProjectileMovement)
	{
		if (!ProjectileLaunchInitialization::ApplyWorldVelocity(ProjectileMovement, LaunchVelocity,
			FMath::Max(ProjectileMovement->MaxSpeed, LaunchVelocity.Size()))) return;
		ProjectileMovement->Activate(true);
	}
}

bool AGravityVortexProjectile::QueryWaterSurfaceAtLocation(const FVector& Location, float& OutWaterSurfaceZ) const
{
	return FWaterSurfaceQueryLibrary::QueryWaterSurface(
		GetWorld(), Location, OutWaterSurfaceZ, bIncludeWaveHeight);
}

void AGravityVortexProjectile::ActivateAtWaterSurface(const FVector& SurfaceLocation)
{
	if (!HasAuthority() || bActivated)
	{
		return;
	}

	bActivated = true;
	TSubclassOf<AGravityVortexField> EffectiveFieldClass = FieldClass;
	if (!EffectiveFieldClass)
	{
		EffectiveFieldClass = AGravityVortexField::StaticClass();
	}
	if (EffectiveFieldClass)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = GetOwner();
		SpawnParams.Instigator = GetInstigator();
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AGravityVortexField* Field = GetWorld()->SpawnActor<AGravityVortexField>(
			EffectiveFieldClass, SurfaceLocation, FRotator::ZeroRotator, SpawnParams))
		{
			// A Blueprint child may have serialized an older replication default.
			Field->SetReplicates(true);
			Field->bAlwaysRelevant = true;
			Field->SetActorTickEnabled(true);
			Field->ForceNetUpdate();
		}
	}

	Destroy();
}
