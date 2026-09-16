// Fill out your copyright notice in the Description page of Project Settings.

#include "Cannonball.h"
#include "Net/UnrealNetwork.h"
#include "WaterSurfaceQueryLibrary.h"
#include "CannonballImpactReceiver.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Ship.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "BaseGameplayTags.h"
#include "GameplayEffect.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "WaterBodyActor.h"
#include "BaseAttributeSet.h"
#include "CollisionChannels.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "RippleSubsystem.h"
#include "GAS/SWCombatEffectContextLibrary.h"
#include "Effects/SWNiagaraScaleLibrary.h"

namespace
{
	TAutoConsoleVariable<int32> CVarCannonCollisionDiagnostics(
		TEXT("sw.CannonCollisionDiagnostics"), 1,
		TEXT("Cannonball collision logs: 0=off, 1=initialization/events and one nearby-hull snapshot per ship on authority."), ECVF_Default);
	TAutoConsoleVariable<int32> CVarCannonWaterImpactDiagnostics(
		TEXT("sw.CannonWaterImpactDiagnostics"),
		1,
		TEXT("Logs cannonball water-overlap and water-impact Niagara spawning. 0=off, 1=on."),
		ECVF_Default);

	const TCHAR* GetCannonWaterNetMode(const UWorld* World)
	{
		if (!World) return TEXT("NoWorld");
		switch (World->GetNetMode())
		{
		case NM_Standalone: return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("Unknown");
		}
	}
}

ACannonball::ACannonball()
{
	PrimaryActorTick.bCanEverTick = true;

	// Sphere Collision
	SphereCollision = CreateDefaultSubobject<USphereComponent>(TEXT("SphereCollision"));
	SphereCollision->InitSphereRadius(15.0f);
	// SpawnActor calls BeginPlay before ACannon can inject the launching ship/team.
	// Keep collision disabled until InitializeProjectile configures all ignores.
	SphereCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SphereCollision->SetGenerateOverlapEvents(true);
	SphereCollision->SetNotifyRigidBodyCollision(true);
	RootComponent = SphereCollision;

	// Water uses overlap; opposing ShipDamage hulls use ProjectileMovement sweep hits.
	SphereCollision->OnComponentBeginOverlap.AddUniqueDynamic(this, &ACannonball::OnOverlapBegin);
	SphereCollision->OnComponentHit.AddUniqueDynamic(this, &ACannonball::OnHit);

	// Visual Mesh
	CannonballMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CannonballMesh"));
	CannonballMesh->SetupAttachment(SphereCollision);
	CannonballMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // Only sphere handles collision

	// Projectile Movement Component
	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = SphereCollision;
	ProjectileMovement->InitialSpeed = 3000.0f;
	ProjectileMovement->MaxSpeed = 5000.0f;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = false;
	ProjectileMovement->bSweepCollision = true;
	ProjectileMovement->ProjectileGravityScale = 1.0f; // Enable parabola arc trajectory
	// Replicated actor movement corrects the collision root. Let the visible child
	// lag smoothly behind those corrections instead of snapping with the root.
	ProjectileMovement->bInterpMovement = true;
	ProjectileMovement->bInterpRotation = true;
	ProjectileMovement->InterpLocationTime = 0.05f;
	ProjectileMovement->InterpRotationTime = 0.05f;
	// At 10,000 cm/s, the engine defaults (300 cm max lag / 500 cm snap)
	// turn a normal 50 ms packet interval into a visible hard snap.
	ProjectileMovement->InterpLocationMaxLagDistance = 2000.0f;
	ProjectileMovement->InterpLocationSnapToTargetDistance = 10000.0f;
	ProjectileMovement->SetInterpolatedComponent(CannonballMesh);
	ProjectileMovement->OnProjectileStop.AddUniqueDynamic(this, &ACannonball::OnProjectileStop);

	ProjectileEffectComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ProjectileEffect"));
	ProjectileEffectComponent->SetupAttachment(CannonballMesh);
	ProjectileEffectComponent->SetAutoActivate(false);

	bReplicates = true;
	SetReplicateMovement(true);
	bAlwaysRelevant = true;
	// Keep adaptive replication from dropping short-lived, high-speed projectiles
	// toward AActor's 2 Hz minimum. The maximum remains the engine default 100 Hz.
	SetMinNetUpdateFrequency(30.0f);
}

void ACannonball::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACannonball, LaunchingShip);
}

void ACannonball::PreInitializeComponents()
{
	Super::PreInitializeComponents();
	// Blueprint defaults can re-enable collision before the launching ship arrives.
	SphereCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ACannonball::OnRep_LaunchingShip()
{
	ConfigureProjectileCollision();
	LogCollisionDiagnostics(TEXT("LAUNCH-SHIP-REPLICATED"));
}

void ACannonball::ConfigureProjectileCollision()
{
	if (SphereCollision && LaunchingShip)
	{
		const bool bEnemyProjectile = LaunchingShip->ActorHasTag(TEXT("Enemy"));
		SphereCollision->SetCollisionProfileName(
			bEnemyProjectile ? TEXT("EnemyCannonball") : TEXT("PlayerCannonball"),
			false);
		SphereCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SphereCollision->SetCollisionObjectType(
			bEnemyProjectile ? ECC_GameTraceChannel3 : ECC_GameTraceChannel2);
		SphereCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
		// WaterBody is WorldStatic and must keep generating the server-authoritative
		// actor overlap used by URippleSubsystem.
		SphereCollision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
		SphereCollision->SetCollisionResponseToChannel(ECC_ShipDamage, ECR_Block);
		SphereCollision->SetCollisionResponseToChannel(ECC_EnemyShipObstacle, ECR_Block);
		SphereCollision->SetGenerateOverlapEvents(true);
		SphereCollision->SetNotifyRigidBodyCollision(true);

		SphereCollision->IgnoreActorWhenMoving(LaunchingShip, true);
		if (AActor* OwnerActor = GetOwner())
		{
			SphereCollision->IgnoreActorWhenMoving(OwnerActor, true);
		}
		if (APawn* InstigatorPawn = GetInstigator())
		{
			SphereCollision->IgnoreActorWhenMoving(InstigatorPawn, true);
		}

		SphereCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
}

void ACannonball::BeginPlay()
{
	Super::BeginPlay();
	// Reapply after Blueprint BeginPlay, without resetting replicated flight velocity.
	SphereCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ConfigureProjectileCollision();
	PreviousDiagnosticLocation = GetActorLocation();
	LogCollisionDiagnostics(TEXT("BEGIN"));
	PreviousProjectileLocation = GetActorLocation();
	PreviousWaterProbeLocation = GetActorLocation();

	if (ProjectileEffectComponent && GetNetMode() != NM_DedicatedServer)
	{
		if (UNiagaraSystem* Effect = GetProjectileEffect())
		{
			ProjectileEffectComponent->SetAsset(Effect);
			USWNiagaraScaleLibrary::ApplyEffectTuning(
				ProjectileEffectComponent,
				GetProjectileEffectScale(),
				GetProjectileEffectLifetimeScale(),
				GetProjectileEffectPlaybackSpeed());
			ProjectileEffectComponent->Activate(true);
		}
	}
}

void ACannonball::PostNetReceiveLocationAndRotation()
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

void ACannonball::PostNetReceiveVelocity(const FVector& NewVelocity)
{
	Super::PostNetReceiveVelocity(NewVelocity);

	// AActor's default implementation does not feed replicated velocity into a
	// ProjectileMovementComponent. Without this, simulated clients travel at the
	// Blueprint default speed and are repeatedly snapped back by server updates.
	if (ProjectileMovement && ProjectileMovement->IsActive())
	{
		ProjectileMovement->Velocity = NewVelocity;
		ProjectileMovement->UpdateComponentVelocity();
	}
}

void ACannonball::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (HasAuthority() && CVarCannonCollisionDiagnostics.GetValueOnGameThread() != 0)
	{
		const FVector Current = GetActorLocation();
		for (TActorIterator<AShip> It(GetWorld()); It; ++It)
		{
			AShip* Ship = *It;
			if (Ship == LaunchingShip || DiagnosticNearbyShips.Contains(TWeakObjectPtr<AShip>(Ship))) continue;
			UStaticMeshComponent* Hull = Ship->ShipDamageMesh;
			if (!Hull) continue;
			const FBox Box = Hull->Bounds.GetBox().ExpandBy(100.0f);
			if (Box.IsInsideOrOn(Current) || FMath::LineBoxIntersection(Box, PreviousDiagnosticLocation, Current, Current - PreviousDiagnosticLocation))
			{
				DiagnosticNearbyShips.Add(TWeakObjectPtr<AShip>(Ship));
				LogCollisionDiagnostics(TEXT("NEAR-HULL"), Ship, Hull);
			}
		}
		PreviousDiagnosticLocation = Current;
	}

	if (!bHasHitWater)
	{
		const FVector CurrentLocation = GetActorLocation();
		float CurrentSurfaceZ = 0.0f;
		if (FWaterSurfaceQueryLibrary::QueryWaterSurface(
			GetWorld(), CurrentLocation, CurrentSurfaceZ, true))
		{
			if (bHasPreviousWaterProbe
				&& PreviousWaterProbeLocation.Z > PreviousWaterProbeSurfaceZ
				&& CurrentLocation.Z <= CurrentSurfaceZ)
			{
				if (CVarCannonWaterImpactDiagnostics.GetValueOnGameThread() != 0)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[CANNON-WATER-VFX][SURFACE-FALLBACK] NetMode=%s Authority=%s Actor=%s Previous=%s PreviousSurfaceZ=%.2f Current=%s CurrentSurfaceZ=%.2f Velocity=%s CollisionProfile=%s CollisionEnabled=%d GenerateOverlap=%s WorldStaticResponse=%d"),
						GetCannonWaterNetMode(GetWorld()), HasAuthority() ? TEXT("true") : TEXT("false"),
						*GetName(), *PreviousWaterProbeLocation.ToCompactString(), PreviousWaterProbeSurfaceZ,
						*CurrentLocation.ToCompactString(), CurrentSurfaceZ, *GetVelocity().ToCompactString(),
						SphereCollision ? *SphereCollision->GetCollisionProfileName().ToString() : TEXT("None"),
						SphereCollision ? static_cast<int32>(SphereCollision->GetCollisionEnabled()) : -1,
						SphereCollision && SphereCollision->GetGenerateOverlapEvents() ? TEXT("true") : TEXT("false"),
						SphereCollision ? static_cast<int32>(SphereCollision->GetCollisionResponseToChannel(ECC_WorldStatic)) : -1);
				}

				FVector SurfaceLocation = CurrentLocation;
				SurfaceLocation.Z = CurrentSurfaceZ;
				TriggerWaterRipple(SurfaceLocation);
			}

			PreviousWaterProbeLocation = CurrentLocation;
			PreviousWaterProbeSurfaceZ = CurrentSurfaceZ;
			bHasPreviousWaterProbe = true;
		}

	}

	if (bHasDesignatedImpact && !bHasHitWater && !bHasProcessedShipHit)
	{
		const FVector CurrentLocation = GetActorLocation();
		if (FMath::PointDistToSegment(
			DesignatedImpactLocation, PreviousProjectileLocation, CurrentLocation)
			<= FMath::Max(1.0f, DesignatedImpactTolerance))
		{
			bHasDesignatedImpact = false;
			SetActorLocation(DesignatedImpactLocation, false, nullptr, ETeleportType::TeleportPhysics);
			DeactivateProjectile();
			SetLifeSpan(0.1f);
			return;
		}
		PreviousProjectileLocation = CurrentLocation;
	}
}

void ACannonball::SetDesignatedImpactLocation(const FVector& InImpactLocation, float InArrivalTolerance)
{
	DesignatedImpactLocation = InImpactLocation;
	DesignatedImpactTolerance = FMath::Max(1.0f, InArrivalTolerance);
	PreviousProjectileLocation = GetActorLocation();
	bHasDesignatedImpact = !InImpactLocation.ContainsNaN();
}

void ACannonball::InitializeProjectile(
	AShip* InLaunchingShip,
	float InDamage,
	float InSpeed,
	const FVector& InInheritedVelocity)
{
	LaunchingShip = InLaunchingShip;
	DamageAmount = InDamage;

	ConfigureProjectileCollision();

	if (ProjectileMovement)
	{
		ProjectileMovement->InitialSpeed = InSpeed;
		ProjectileMovement->Velocity = GetActorForwardVector() * InSpeed + InInheritedVelocity;
		ProjectileMovement->MaxSpeed = FMath::Max(
			ProjectileMovement->Velocity.Size() * 2.0f,
			5000.0f);
		ProjectileMovement->UpdateComponentVelocity();
		// Never carry interpolation offset into the projectile's first visible frame.
		ProjectileMovement->ResetInterpolation();
	}
	LogCollisionDiagnostics(TEXT("INITIALIZED"));
}

void ACannonball::LogCollisionDiagnostics(const TCHAR* Event, AActor* OtherActor, UPrimitiveComponent* OtherComp) const
{
	if (CVarCannonCollisionDiagnostics.GetValueOnGameThread() == 0) return;
	UE_LOG(LogTemp, Warning,
		TEXT("[CANNON-COLLISION][%s] Net=%s Authority=%d Ball=%s Class=%s LaunchShip=%s LaunchEnemy=%d Owner=%s Instigator=%s ActorCollision=%d Root=%s Updated=%s Profile=%s Enabled=%d Object=%d ShipDamageResponse=%d Active=%d Sweep=%d Position=%s Velocity=%s Water=%d ShipHit=%d BlockingHit=%d"),
		Event, GetCannonWaterNetMode(GetWorld()), HasAuthority(), *GetName(), *GetClass()->GetName(),
		*GetNameSafe(LaunchingShip), LaunchingShip && LaunchingShip->ActorHasTag(TEXT("Enemy")),
		*GetNameSafe(GetOwner()), *GetNameSafe(GetInstigator()), GetActorEnableCollision(),
		*GetNameSafe(GetRootComponent()), *GetNameSafe(ProjectileMovement ? ProjectileMovement->UpdatedComponent.Get() : nullptr),
		SphereCollision ? *SphereCollision->GetCollisionProfileName().ToString() : TEXT("None"),
		SphereCollision ? (int32)SphereCollision->GetCollisionEnabled() : -1,
		SphereCollision ? (int32)SphereCollision->GetCollisionObjectType() : -1,
		SphereCollision ? (int32)SphereCollision->GetCollisionResponseToChannel(ECC_ShipDamage) : -1,
		ProjectileMovement && ProjectileMovement->IsActive(), ProjectileMovement && ProjectileMovement->bSweepCollision,
		*GetActorLocation().ToCompactString(), *GetVelocity().ToCompactString(), bHasHitWater, bHasProcessedShipHit, bHasProcessedBlockingImpact);
	if (!OtherComp) return;
	const UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(OtherComp);
	const UStaticMesh* Mesh = MeshComp ? MeshComp->GetStaticMesh() : nullptr;
	const UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
	UE_LOG(LogTemp, Warning,
		TEXT("[CANNON-COLLISION][%s-TARGET] Ball=%s Actor=%s Enemy=%d ActorCollision=%d Component=%s Registered=%d Profile=%s Enabled=%d Object=%d TargetToBall=%d BallToTarget=%d Overlaps=%d Mesh=%s SimpleShapes=%d Complexity=%d Bounds=%s Extent=%s"),
		Event, *GetName(), *GetNameSafe(OtherActor), OtherActor && OtherActor->ActorHasTag(TEXT("Enemy")),
		OtherActor && OtherActor->GetActorEnableCollision(), *OtherComp->GetName(), OtherComp->IsRegistered(),
		*OtherComp->GetCollisionProfileName().ToString(), (int32)OtherComp->GetCollisionEnabled(), (int32)OtherComp->GetCollisionObjectType(),
		SphereCollision ? (int32)OtherComp->GetCollisionResponseToChannel(SphereCollision->GetCollisionObjectType()) : -1,
		SphereCollision ? (int32)SphereCollision->GetCollisionResponseToChannel(OtherComp->GetCollisionObjectType()) : -1,
		OtherComp->GetGenerateOverlapEvents(), *GetNameSafe(Mesh), Body ? Body->AggGeom.GetElementCount() : -1,
		Body ? (int32)Body->CollisionTraceFlag : -1, *OtherComp->Bounds.Origin.ToCompactString(), *OtherComp->Bounds.BoxExtent.ToCompactString());
}

void ACannonball::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor || OtherActor == this) return;
	LogCollisionDiagnostics(TEXT("OVERLAP"), OtherActor, OtherComp);

	// Hit Water (Check AWaterBody class or Water profile name)
	bool bIsWater = false;
	if (OtherActor->IsA(AWaterBody::StaticClass()))
	{
		bIsWater = true;
	}
	else if (OtherComp && (OtherComp->GetCollisionProfileName().ToString().Contains(TEXT("Water")) || OtherComp->GetName().Contains(TEXT("Water"))))
	{
		bIsWater = true;
	}

	if (bIsWater)
	{
		if (!bHasHitWater)
		{
			HandleWaterOverlap(OtherActor, OtherComp, bFromSweep, SweepResult);
		}
		return;
	}

	// Pawn, Storage, terrain and every non-water overlap are intentionally ignored.
}

void ACannonball::OnHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	LogCollisionDiagnostics(TEXT("HIT"), OtherActor, OtherComp);
	HandleBlockingImpact(OtherActor, OtherComp, Hit);
}

void ACannonball::OnProjectileStop(const FHitResult& ImpactResult)
{
	LogCollisionDiagnostics(TEXT("STOP"), ImpactResult.GetActor(), ImpactResult.GetComponent());
	HandleBlockingImpact(ImpactResult.GetActor(), ImpactResult.GetComponent(), ImpactResult);
}

void ACannonball::HandleBlockingImpact(
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	const FHitResult& Hit)
{
	if (!OtherActor || OtherActor == this || OtherActor == LaunchingShip
		|| OtherActor == GetOwner() || OtherActor == GetInstigator()
		|| bHasProcessedBlockingImpact)
	{
		LogCollisionDiagnostics(TEXT("IMPACT-REJECTED"), OtherActor, OtherComp);
		return;
	}

	if (AShip* HitShip = Cast<AShip>(OtherActor))
	{
		if (bHasProcessedShipHit)
		{
			return;
		}

		bHasProcessedBlockingImpact = true;
		bHasProcessedShipHit = true;
		HandleShipImpact(HitShip, Hit);
	}
	else if (OtherComp && OtherComp->GetCollisionObjectType() == ECC_EnemyShipObstacle)
	{
		bHasProcessedBlockingImpact = true;
		if (OtherActor->GetClass()->ImplementsInterface(UCannonballImpactReceiver::StaticClass()))
		{
			ICannonballImpactReceiver::Execute_ReceiveCannonballImpact(OtherActor, this);
		}
		UE_LOG(LogTemp, Warning,
			TEXT("ACannonball: Exploded on obstacle %s at %s."),
			*GetNameSafe(OtherActor),
			*Hit.ImpactPoint.ToCompactString());
		Destroy();
	}
}

void ACannonball::HandleShipImpact(AShip* HitShip, const FHitResult& Hit)
{
	HandleShipHit(HitShip);
}

void ACannonball::HandleShipHit(AShip* HitShip)
{
	if (!HasAuthority() || !HitShip || HitShip == LaunchingShip)
	{
		LogCollisionDiagnostics(TEXT("DAMAGE-REJECTED"), HitShip);
		return;
	}

	// Collision responses already enforce this, but keep a gameplay-level team
	// check so a bad Blueprint collision override can never cause friendly fire.
	if (LaunchingShip
		&& LaunchingShip->ActorHasTag(TEXT("Enemy")) == HitShip->ActorHasTag(TEXT("Enemy")))
	{
		LogCollisionDiagnostics(TEXT("SAME-TEAM-REJECTED"), HitShip, HitShip->ShipDamageMesh);
		return;
	}

	SpawnNiagaraEffectForAll(ShipImpactEffect, GetActorLocation(), ShipImpactEffectScale,
		ShipImpactEffectLifetimeScale, ShipImpactEffectPlaybackSpeed);

	const FVector ExplosionLocation = GetActorLocation();
	const float EffectiveRadius = FMath::Max(0.0f, SplashDamageRadius);
	DrawDebugSphere(GetWorld(), ExplosionLocation, EffectiveRadius, 24, FColor::Red, false, 2.0f);

	// Object queries do not depend on the projectile collision profile's response
	// table. Pawn capsules and ShipDamage hulls therefore remain compatible without
	// opening either side up to unintended projectile blocking/overlaps.
	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_Pawn);
	ObjectQuery.AddObjectTypesToQuery(ECC_ShipDamage);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CannonballSplashDamage), false, this);
	if (LaunchingShip)
	{
		QueryParams.AddIgnoredActor(LaunchingShip);
	}

	TArray<FOverlapResult> Overlaps;
	if (EffectiveRadius > 0.0f)
	{
		GetWorld()->OverlapMultiByObjectType(
			Overlaps,
			ExplosionLocation,
			FQuat::Identity,
			ObjectQuery,
			FCollisionShape::MakeSphere(EffectiveRadius),
			QueryParams);
	}

	TSet<AActor*> UniqueTargets;
	// A malformed collision setup must not make the directly struck opposing ship
	// escape damage, even if its hull was omitted from the overlap result.
	UniqueTargets.Add(HitShip);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (AActor* Candidate = Overlap.GetActor())
		{
			UniqueTargets.Add(Candidate);
		}
	}

	int32 DamagedTargetCount = 0;
	for (AActor* Target : UniqueTargets)
	{
		// A ship impact damages the hull, but never its owned/attached crew through
		// splash. This remains module-independent and also covers authored crew.
		if (Target != HitShip
			&& (Target->GetOwner() == HitShip || Target->IsAttachedTo(HitShip)))
		{
			continue;
		}
		if (IsOpposingSplashTarget(Target) && ApplyDamageToTarget(Target))
		{
			++DamagedTargetCount;
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("ACannonball: Splash exploded on %s. Radius=%.1f Damage=%.1f Targets=%d"),
		*HitShip->GetName(),
		EffectiveRadius,
		DamageAmount,
		DamagedTargetCount);
	Destroy();
}

bool ACannonball::IsOpposingSplashTarget(const AActor* Candidate) const
{
	if (!Candidate || Candidate == this || Candidate == LaunchingShip
		|| Candidate == GetOwner() || Candidate == GetInstigator())
	{
		return false;
	}

	const bool bEnemyProjectile = LaunchingShip && LaunchingShip->ActorHasTag(TEXT("Enemy"));
	if (const AShip* CandidateShip = Cast<AShip>(Candidate))
	{
		return CandidateShip->ActorHasTag(TEXT("Enemy")) != bEnemyProjectile;
	}

	UAbilitySystemComponent* CandidateASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Candidate));
	return CandidateASC && CandidateASC->HasMatchingGameplayTag(
		bEnemyProjectile ? Team_Player : Team_Enemy);
}

bool ACannonball::ApplyDamageToTarget(AActor* TargetActor)
{
	UAbilitySystemComponent* TargetASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
	if (!TargetASC || !DamageGEClass)
	{
		return false;
	}

	UAbilitySystemComponent* SourceASC = LaunchingShip
		? LaunchingShip->GetAbilitySystemComponent()
		: nullptr;
	if (!SourceASC)
	{
		SourceASC = TargetASC;
	}

	FGameplayEffectContextHandle EffectContext =
		USWCombatEffectContextLibrary::MakeCombatEffectContext(
			SourceASC,
			GetInstigator(),
			this,
			TargetActor,
			false,
			FHitResult(),
			GetVelocity());
	USWCombatEffectContextLibrary::SetDamageDeliveryType(
		EffectContext, ESWDamageDeliveryType::DirectHit);
	FGameplayEffectSpecHandle SpecHandle =
		SourceASC->MakeOutgoingSpec(DamageGEClass, 1.0f, EffectContext);
	if (!SpecHandle.IsValid())
	{
		return false;
	}

	float TargetDamage = DamageAmount;
	if (!Cast<AShip>(TargetActor))
	{
		if (TargetASC->HasMatchingGameplayTag(Team_Enemy))
		{
			TargetDamage *= FMath::Max(0.0f, EnemyDamageMultiplier);
		}
		else if (TargetASC->HasMatchingGameplayTag(Team_Player))
		{
			TargetDamage *= FMath::Max(0.0f, PlayerDamageMultiplier);
		}
	}

	SpecHandle.Data.Get()->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(FName("Data.Damage")),
		TargetDamage);
	TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
	return true;
}

void ACannonball::TriggerWaterRipple(const FVector& HitLocation)
{
	if (bHasHitWater)
	{
		if (CVarCannonWaterImpactDiagnostics.GetValueOnGameThread() != 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[CANNON-WATER-VFX][SKIP-DUPLICATE] NetMode=%s Actor=%s Location=%s"),
				GetCannonWaterNetMode(GetWorld()), *GetName(), *HitLocation.ToCompactString());
		}
		return;
	}
	bHasHitWater = true;
	UNiagaraSystem* Effect = GetWaterImpactEffect();
	if (CVarCannonWaterImpactDiagnostics.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CANNON-WATER-VFX][TRIGGER] NetMode=%s Authority=%s Actor=%s Class=%s Effect=%s Location=%s Velocity=%s Scale=%.3f LifetimeScale=%.3f PlaybackSpeed=%.3f"),
			GetCannonWaterNetMode(GetWorld()), HasAuthority() ? TEXT("true") : TEXT("false"),
			*GetName(), *GetNameSafe(GetClass()), *GetNameSafe(Effect),
			*HitLocation.ToCompactString(), *GetVelocity().ToCompactString(),
			GetWaterImpactEffectScale(), GetWaterImpactEffectLifetimeScale(),
			GetWaterImpactEffectPlaybackSpeed());
	}
	if (HasAuthority())
	{
		if (Effect)
		{
			// This Niagara is authored to burst along its local +Z axis. Keep it
			// aligned with world +Z, independent of the incoming trajectory.
			MulticastSpawnNiagaraEffect(
				Effect,
				HitLocation,
				FRotator::ZeroRotator,
				FMath::Max(0.01f, GetWaterImpactEffectScale()),
				FMath::Max(0.01f, GetWaterImpactEffectLifetimeScale()),
				FMath::Max(0.01f, GetWaterImpactEffectPlaybackSpeed()),
				true);
		}
		else if (CVarCannonWaterImpactDiagnostics.GetValueOnGameThread() != 0)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[CANNON-WATER-VFX][NO-ASSET] Actor=%s Class=%s has no resolved water-impact Niagara."),
				*GetName(), *GetNameSafe(GetClass()));
		}
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RippleDiagnostics")))
	{
		const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
		UE_LOG(LogTemp, Warning,
			TEXT("[RIPPLE-LATENCY][%s] CannonballWaterContact Actor=%s Origin=%s ServerTime=%.6f DownwardSpeed=%.1f"),
			HasAuthority() ? TEXT("Authority") : TEXT("Client"),
			*GetName(),
			*HitLocation.ToString(),
			GameState ? GameState->GetServerWorldTimeSeconds() : 0.0,
			-GetVelocity().Z);
	}
	if (URippleSubsystem* RippleSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<URippleSubsystem>()
		: nullptr)
	{
		const float DownwardSpeed = -GetVelocity().Z;
		if (HasAuthority())
		{
			RippleSubsystem->AddRippleFromImpact(
				FVector2D(HitLocation.X, HitLocation.Y), DownwardSpeed);
		}
		else
		{
			RippleSubsystem->AddPredictedRippleFromImpact(
				FVector2D(HitLocation.X, HitLocation.Y), DownwardSpeed);
		}
	}

	// Schedule disabling physics movement, collision and mesh visibility 0.05 seconds later
	// This ensures the physics engine registers the overlap event with AWaterBody with its original velocity
	// and URippleSubsystem has enough time to spawn the ripple.
	GetWorldTimerManager().SetTimer(WaterHitTimerHandle, this, &ACannonball::DeactivateProjectile, 0.05f, false);

	// Schedule destruction after N seconds
	SetLifeSpan(LifeTimeAfterWaterHit);
}

void ACannonball::SpawnNiagaraEffectForAll(
	UNiagaraSystem* Effect,
	const FVector& Location,
	float SizeScale,
	float LifetimeScale,
	float PlaybackSpeed)
{
	if (!HasAuthority() || !Effect)
	{
		return;
	}

	const FVector TravelDirection = GetVelocity().GetSafeNormal();
	const FRotator EffectRotation = TravelDirection.IsNearlyZero()
		? GetActorRotation()
		: (-TravelDirection).Rotation();
	MulticastSpawnNiagaraEffect(Effect, Location, EffectRotation,
		FMath::Max(0.01f, SizeScale), FMath::Max(0.01f, LifetimeScale),
		FMath::Max(0.01f, PlaybackSpeed), false);
}

UNiagaraSystem* ACannonball::GetProjectileEffect() const
{
	return ProjectileEffect;
}

float ACannonball::GetProjectileEffectScale() const
{
	return ProjectileEffectScale;
}

float ACannonball::GetProjectileEffectLifetimeScale() const { return ProjectileEffectLifetimeScale; }
float ACannonball::GetProjectileEffectPlaybackSpeed() const { return ProjectileEffectPlaybackSpeed; }
UNiagaraSystem* ACannonball::GetWaterImpactEffect() const { return WaterImpactEffect; }
float ACannonball::GetWaterImpactEffectScale() const { return WaterImpactEffectScale; }
float ACannonball::GetWaterImpactEffectLifetimeScale() const { return WaterImpactEffectLifetimeScale; }
float ACannonball::GetWaterImpactEffectPlaybackSpeed() const { return WaterImpactEffectPlaybackSpeed; }

void ACannonball::MulticastSpawnNiagaraEffect_Implementation(
	UNiagaraSystem* Effect,
	FVector_NetQuantize Location,
	FRotator Rotation,
	float SizeScale,
	float LifetimeScale,
	float PlaybackSpeed,
	bool bIsWaterImpact)
{
	UNiagaraComponent* SpawnedComponent = nullptr;
	if (Effect && GetWorld())
	{
		SpawnedComponent = USWNiagaraScaleLibrary::SpawnTunedSystemAtLocation(
			GetWorld(), Effect, Location, Rotation, SizeScale, LifetimeScale, PlaybackSpeed, true);
	}
	if (bIsWaterImpact && CVarCannonWaterImpactDiagnostics.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CANNON-WATER-VFX][MULTICAST-SPAWN] NetMode=%s Actor=%s Effect=%s Location=%s Created=%s Component=%s Active=%s ComponentScale=%s CustomTimeDilation=%.3f"),
			GetCannonWaterNetMode(GetWorld()), *GetName(), *GetNameSafe(Effect),
			*Location.ToString(), SpawnedComponent ? TEXT("true") : TEXT("false"),
			*GetNameSafe(SpawnedComponent),
			SpawnedComponent && SpawnedComponent->IsActive() ? TEXT("true") : TEXT("false"),
			SpawnedComponent ? *SpawnedComponent->GetRelativeScale3D().ToCompactString() : TEXT("N/A"),
			SpawnedComponent ? SpawnedComponent->GetCustomTimeDilation() : 0.0f);
	}
}

void ACannonball::HandleWaterOverlap(
	AActor* WaterActor,
	UPrimitiveComponent* WaterComponent,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	FVector SurfaceLocation = GetActorLocation();
	const FVector ProjectileLocation = SurfaceLocation;
	float SurfaceZ = 0.0f;
	const AWaterBody* WaterBody = Cast<AWaterBody>(WaterActor);
	const bool bSurfaceQuerySucceeded = WaterBody && FWaterSurfaceQueryLibrary::QueryWaterBodySurface(
		WaterBody, SurfaceLocation,
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0,
		SurfaceZ, true);
	if (bSurfaceQuerySucceeded)
	{
		SurfaceLocation.Z = SurfaceZ;
	}
	else if (bFromSweep && !SweepResult.ImpactPoint.IsNearlyZero())
	{
		SurfaceLocation = SweepResult.ImpactPoint;
	}
	if (CVarCannonWaterImpactDiagnostics.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CANNON-WATER-VFX][OVERLAP] NetMode=%s Authority=%s Actor=%s WaterActor=%s WaterComponent=%s FromSweep=%s BlockingHit=%s AlreadyHandled=%s ProjectileLocation=%s SweepImpact=%s SurfaceQuery=%s SpawnLocation=%s"),
			GetCannonWaterNetMode(GetWorld()), HasAuthority() ? TEXT("true") : TEXT("false"),
			*GetName(), *GetNameSafe(WaterActor), *GetNameSafe(WaterComponent),
			bFromSweep ? TEXT("true") : TEXT("false"),
			SweepResult.bBlockingHit ? TEXT("true") : TEXT("false"),
			bHasHitWater ? TEXT("true") : TEXT("false"),
			*ProjectileLocation.ToCompactString(), *SweepResult.ImpactPoint.ToCompactString(),
			bSurfaceQuerySucceeded ? TEXT("true") : TEXT("false"),
			*SurfaceLocation.ToCompactString());
	}
	TriggerWaterRipple(SurfaceLocation);
}

void ACannonball::MarkWaterHitHandledWithoutDeactivation()
{
	bHasHitWater = true;
	bHasDesignatedImpact = false;
	GetWorldTimerManager().ClearTimer(WaterHitTimerHandle);
}

void ACannonball::DeactivateProjectile()
{
	// Stop projectile physical movement
	if (ProjectileMovement)
	{
		ProjectileMovement->StopMovementImmediately();
		ProjectileMovement->Deactivate();
	}

	if (SphereCollision)
	{
		SphereCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (CannonballMesh)
	{
		CannonballMesh->SetVisibility(false);
	}

	if (ProjectileEffectComponent)
	{
		ProjectileEffectComponent->Deactivate();
	}
}
