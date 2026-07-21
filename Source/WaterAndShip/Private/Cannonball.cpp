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

	bReplicates = true;
	SetReplicateMovement(true);
	bAlwaysRelevant = true;
}

void ACannonball::BeginPlay()
{
	Super::BeginPlay();
}

void ACannonball::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
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
			// UE_LOG(LogTemp, Warning, TEXT("ACannonball: Hit Water at Location %s! Triggering Ripple..."), *GetActorLocation().ToString());
			TriggerWaterRipple(GetActorLocation());
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
		FGameplayEffectContextHandle EffectContext = TargetASC->MakeEffectContext();
		EffectContext.AddInstigator(GetInstigator(), this);

		FGameplayEffectSpecHandle SpecHandle = TargetASC->MakeOutgoingSpec(DamageGEClass, 1.0f, EffectContext);
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

	// Schedule disabling physics movement, collision and mesh visibility 0.05 seconds later
	// This ensures the physics engine registers the overlap event with AWaterBody with its original velocity
	// and URippleSubsystem has enough time to spawn the ripple.
	GetWorldTimerManager().SetTimer(WaterHitTimerHandle, this, &ACannonball::DeactivateProjectile, 0.05f, false);

	// Schedule destruction after N seconds
	SetLifeSpan(LifeTimeAfterWaterHit);
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
