// Fill out your copyright notice in the Description page of Project Settings.

#include "Cannonball.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Ship.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "DrawDebugHelpers.h"
#include "WaterBodyActor.h"
#include "BaseAttributeSet.h"
#include "CollisionChannels.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RippleSubsystem.h"
#include "GAS/SWCombatEffectContextLibrary.h"

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

	bReplicates = true;
	SetReplicateMovement(true);
	bAlwaysRelevant = true;
	// Keep adaptive replication from dropping short-lived, high-speed projectiles
	// toward AActor's 2 Hz minimum. The maximum remains the engine default 100 Hz.
	SetMinNetUpdateFrequency(30.0f);
}

void ACannonball::BeginPlay()
{
	Super::BeginPlay();
	PreviousProjectileLocation = GetActorLocation();
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

void ACannonball::InitializeProjectile(AShip* InLaunchingShip, float InDamage, float InSpeed)
{
	LaunchingShip = InLaunchingShip;
	DamageAmount = InDamage;

	if (SphereCollision && InLaunchingShip)
	{
		const bool bEnemyProjectile = InLaunchingShip->ActorHasTag(TEXT("Enemy"));
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
		if (!bEnemyProjectile)
		{
			SphereCollision->SetCollisionResponseToChannel(ECC_EnemyShipObstacle, ECR_Block);
		}
		SphereCollision->SetGenerateOverlapEvents(true);
		SphereCollision->SetNotifyRigidBodyCollision(true);

		SphereCollision->IgnoreActorWhenMoving(InLaunchingShip, true);
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

	if (ProjectileMovement)
	{
		ProjectileMovement->InitialSpeed = InSpeed;
		ProjectileMovement->MaxSpeed = FMath::Max(InSpeed * 2.0f, 5000.0f);
		ProjectileMovement->Velocity = GetActorForwardVector() * InSpeed;
		ProjectileMovement->UpdateComponentVelocity();
	}
}

void ACannonball::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor || OtherActor == this) return;

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
	if (!OtherActor || OtherActor == this || OtherActor == LaunchingShip
		|| OtherActor == GetOwner() || OtherActor == GetInstigator())
	{
		return;
	}

	if (AShip* HitShip = Cast<AShip>(OtherActor))
	{
		if (bHasProcessedShipHit)
		{
			return;
		}

		bHasProcessedShipHit = true;
		HandleShipHit(HitShip);
	}
	else if (OtherComp && OtherComp->GetCollisionObjectType() == ECC_EnemyShipObstacle)
	{
		// Enemy obstacles are projectile shields. Only PlayerCannon projectiles can
		// block against this channel, so consuming the projectile here is team-safe.
		Destroy();
	}
}

void ACannonball::HandleShipHit(AShip* HitShip)
{
	if (!HasAuthority() || !HitShip || HitShip == LaunchingShip)
	{
		return;
	}

	// Collision responses already enforce this, but keep a gameplay-level team
	// check so a bad Blueprint collision override can never cause friendly fire.
	if (LaunchingShip
		&& LaunchingShip->ActorHasTag(TEXT("Enemy")) == HitShip->ActorHasTag(TEXT("Enemy")))
	{
		return;
	}

	// Preserve the existing GAS damage path exactly; only contact detection changed.
	DrawDebugSphere(GetWorld(), GetActorLocation(), 100.0f, 12, FColor::Red, false, 2.0f);

	UAbilitySystemComponent* TargetASC = HitShip->GetAbilitySystemComponent();
	if (TargetASC && DamageGEClass)
	{
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
				HitShip,
				false,
				FHitResult(),
				GetVelocity());

		FGameplayEffectSpecHandle SpecHandle = SourceASC->MakeOutgoingSpec(DamageGEClass, 1.0f, EffectContext);
		if (SpecHandle.IsValid())
		{
			SpecHandle.Data.Get()->SetSetByCallerMagnitude(
				FGameplayTag::RequestGameplayTag(FName("Data.Damage")),
				DamageAmount);
			TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
		}
	}

	float CurrentHealth = 0.0f;
	if (TargetASC)
	{
		CurrentHealth = TargetASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute());
	}

	UE_LOG(LogTemp, Warning,
		TEXT("ACannonball: Swept Hit Ship %s! Dealt %f damage. Current Health: %f"),
		*HitShip->GetName(),
		DamageAmount,
		CurrentHealth);
	Destroy();
}

void ACannonball::TriggerWaterRipple(const FVector& HitLocation)
{
	if (bHasHitWater) return;
	bHasHitWater = true;

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
	if (!HasAuthority())
	{
		if (URippleSubsystem* RippleSubsystem = GetWorld()
			? GetWorld()->GetSubsystem<URippleSubsystem>()
			: nullptr)
		{
			RippleSubsystem->AddPredictedRippleFromImpact(
				FVector2D(HitLocation.X, HitLocation.Y),
				-GetVelocity().Z);
		}
	}

	// Schedule disabling physics movement, collision and mesh visibility 0.05 seconds later
	// This ensures the physics engine registers the overlap event with AWaterBody with its original velocity
	// and URippleSubsystem has enough time to spawn the ripple.
	GetWorldTimerManager().SetTimer(WaterHitTimerHandle, this, &ACannonball::DeactivateProjectile, 0.05f, false);

	// Schedule destruction after N seconds
	SetLifeSpan(LifeTimeAfterWaterHit);
}

void ACannonball::HandleWaterOverlap(
	AActor* WaterActor,
	UPrimitiveComponent* WaterComponent,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	TriggerWaterRipple(GetActorLocation());
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
}
