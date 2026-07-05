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
#include "Cannon.h"

ACannonball::ACannonball()
{
	PrimaryActorTick.bCanEverTick = true;

	// Sphere Collision
	SphereCollision = CreateDefaultSubobject<USphereComponent>(TEXT("SphereCollision"));
	SphereCollision->InitSphereRadius(15.0f);
	SphereCollision->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	RootComponent = SphereCollision;

	// Overlap Event
	SphereCollision->OnComponentBeginOverlap.AddUniqueDynamic(this, &ACannonball::OnOverlapBegin);

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
	ProjectileMovement->ProjectileGravityScale = 1.0f; // Enable parabola arc trajectory

	bReplicates = true;
	SetReplicateMovement(true);
}

void ACannonball::BeginPlay()
{
	Super::BeginPlay();
}

void ACannonball::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ACannonball::InitializeProjectile(AShip* InLaunchingShip, float InDamage)
{
	LaunchingShip = InLaunchingShip;
	DamageAmount = InDamage;
}

void ACannonball::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor || OtherActor == this) return;

	// 1. Ignore collision with launching ship
	if (OtherActor == LaunchingShip) return;

	// 2. Ignore collision with own owner or instigator
	if (OtherActor == GetOwner() || OtherActor == GetInstigator()) return;

	// 3. Ignore collision with cannons attached to launching ship
	if (ACannon* HitCannon = Cast<ACannon>(OtherActor))
	{
		if (HitCannon == GetOwner() || HitCannon->GetAttachParentActor() == LaunchingShip || HitCannon->GetParentActor() == LaunchingShip)
		{
			return;
		}
	}

	// Hit Ship
	if (AShip* HitShip = Cast<AShip>(OtherActor))
	{
		if (HasAuthority())
		{
			// 1. Draw debug sphere
			DrawDebugSphere(GetWorld(), GetActorLocation(), 100.0f, 12, FColor::Red, false, 2.0f);

			// 2. Apply damage via GAS
			UAbilitySystemComponent* TargetASC = HitShip->GetAbilitySystemComponent();
			if (TargetASC && DamageGEClass)
			{
				FGameplayEffectContextHandle EffectContext = TargetASC->MakeEffectContext();
				EffectContext.AddInstigator(GetInstigator(), this);

				FGameplayEffectSpecHandle SpecHandle = TargetASC->MakeOutgoingSpec(DamageGEClass, 1.0f, EffectContext);
				if (SpecHandle.IsValid())
				{
					// Set magnitude dynamically using Data.Damage tag
					SpecHandle.Data.Get()->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(FName("Data.Damage")), DamageAmount);
					TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
				}
			}

			UE_LOG(LogTemp, Log, TEXT("ACannonball: Hit Ship %s! Dealt %f damage."), *HitShip->GetName(), DamageAmount);

			// 3. Destroy projectile immediately
			Destroy();
		}
		return;
	}

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
		TriggerWaterRipple(GetActorLocation());
	}
}

void ACannonball::TriggerWaterRipple(const FVector& HitLocation)
{
	if (bHasHitWater) return;
	bHasHitWater = true;

	// Stop projectile physical movement and hide visual representation
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

	// Schedule destruction after N seconds
	SetLifeSpan(LifeTimeAfterWaterHit);
}
