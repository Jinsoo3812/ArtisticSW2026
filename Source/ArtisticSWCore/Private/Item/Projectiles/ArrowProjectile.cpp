#include "Item/Projectiles/ArrowProjectile.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"

AArrowProjectile::AArrowProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	if (CollisionComp)
	{
		CollisionComp->SetBoxExtent(FVector(8.0f, 2.0f, 2.0f));
		CollisionComp->SetCollisionProfileName(TEXT("Projectile"));
		CollisionComp->SetNotifyRigidBodyCollision(true);
		CollisionComp->OnComponentHit.AddDynamic(this, &AArrowProjectile::OnArrowHit);
	}

	if (ProjectileMovementComp)
	{
		ProjectileMovementComp->bAutoActivate = false;
		ProjectileMovementComp->InitialSpeed = 0.0f;
		ProjectileMovementComp->MaxSpeed = 6000.0f;
		ProjectileMovementComp->ProjectileGravityScale = 0.15f;
		ProjectileMovementComp->bRotationFollowsVelocity = true;
		ProjectileMovementComp->bShouldBounce = false;
	}
}

void AArrowProjectile::BeginPlay()
{
	Super::BeginPlay();

	if (APawn* InstigatorPawn = GetInstigator())
	{
		if (CollisionComp)
		{
			CollisionComp->IgnoreActorWhenMoving(InstigatorPawn, true);
		}
	}

	if (AActor* OwnerActor = GetOwner())
	{
		if (CollisionComp)
		{
			CollisionComp->IgnoreActorWhenMoving(OwnerActor, true);
		}
	}
}

void AArrowProjectile::LaunchArrow(const FVector& LaunchVelocity)
{
	if (ProjectileMovementComp)
	{
		ProjectileMovementComp->Velocity = LaunchVelocity;
		ProjectileMovementComp->Activate();
	}
}

void AArrowProjectile::SetArrowMesh(UStaticMesh* InMesh)
{
	if (MeshComp && InMesh)
	{
		MeshComp->SetStaticMesh(InMesh);
	}
}

void AArrowProjectile::SetDamageEffectSpecHandle(const FGameplayEffectSpecHandle& InDamageEffectSpecHandle)
{
	DamageEffectSpecHandle = InDamageEffectSpecHandle;
}

void AArrowProjectile::SetAdditionalDamageEffectSpecHandles(const TArray<FGameplayEffectSpecHandle>& InAdditionalDamageEffectSpecHandles)
{
	AdditionalDamageEffectSpecHandles = InAdditionalDamageEffectSpecHandles;
}

void AArrowProjectile::Multicast_PlayImpactFX_Implementation(const FHitResult& Hit)
{
	K2_OnImpactFX(Hit);
}

void AArrowProjectile::OnArrowHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!HasAuthority() || ShouldIgnoreHitActor(OtherActor))
	{
		return;
	}

	ApplyDamageToActor(OtherActor);
	Multicast_PlayImpactFX(Hit);

	if (bDestroyOnImpact)
	{
		Destroy();
	}
}

bool AArrowProjectile::ShouldIgnoreHitActor(const AActor* OtherActor) const
{
	if (!OtherActor || OtherActor == this)
	{
		return true;
	}

	if (OtherActor == GetOwner() || OtherActor == GetInstigator())
	{
		return true;
	}

	return false;
}

void AArrowProjectile::ApplyDamageToActor(AActor* TargetActor)
{
	if (!TargetActor)
	{
		return;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
	if (!TargetASC)
	{
		return;
	}

	if (DamageEffectSpecHandle.IsValid() && DamageEffectSpecHandle.Data.IsValid())
	{
		TargetASC->ApplyGameplayEffectSpecToSelf(*DamageEffectSpecHandle.Data.Get());
	}

	for (const FGameplayEffectSpecHandle& AdditionalSpecHandle : AdditionalDamageEffectSpecHandles)
	{
		if (AdditionalSpecHandle.IsValid() && AdditionalSpecHandle.Data.IsValid())
		{
			TargetASC->ApplyGameplayEffectSpecToSelf(*AdditionalSpecHandle.Data.Get());
		}
	}
}
