#include "Projectiles/GravityVortexProjectile.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Skills/GravityVortexField.h"
#include "WaterSurfaceQueryLibrary.h"

AGravityVortexProjectile::AGravityVortexProjectile()
{
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

	FieldClass = AGravityVortexField::StaticClass();
}

void AGravityVortexProjectile::BeginPlay()
{
	Super::BeginPlay();
	PreviousLocation = GetActorLocation();

	if (HasAuthority())
	{
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

	if (!HasAuthority() || bActivated)
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

void AGravityVortexProjectile::LaunchProjectile(const FVector& LaunchVelocity)
{
	if (ProjectileMovement)
	{
		ProjectileMovement->Velocity = LaunchVelocity;
		ProjectileMovement->MaxSpeed = FMath::Max(ProjectileMovement->MaxSpeed, LaunchVelocity.Size());
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
