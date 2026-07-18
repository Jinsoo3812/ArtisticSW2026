#include "Item/Projectiles/ArrowProjectile.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "StatusEffectLibrary.h"

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
		ProjectileMovementComp->ProjectileGravityScale = FlightGravityScale;
		ProjectileMovementComp->bInitialVelocityInLocalSpace = false;
		ProjectileMovementComp->bRotationFollowsVelocity = true;
		ProjectileMovementComp->bShouldBounce = false;
	}
}

void AArrowProjectile::BeginPlay()
{
	Super::BeginPlay();

	if (APawn* InstigatorPawn = GetInstigator())
	{
		IgnoreActorForMovement(InstigatorPawn);
	}

	if (AActor* OwnerActor = GetOwner())
	{
		IgnoreActorForMovement(OwnerActor);
	}
}

void AArrowProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CollisionComp)
	{
		for (const TWeakObjectPtr<AActor>& IgnoredActorPtr : MovementIgnoredActors)
		{
			if (AActor* IgnoredActor = IgnoredActorPtr.Get())
			{
				CollisionComp->IgnoreActorWhenMoving(IgnoredActor, false);

				TArray<UPrimitiveComponent*> PrimitiveComponents;
				IgnoredActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
				for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
				{
					if (PrimitiveComponent)
					{
						PrimitiveComponent->IgnoreActorWhenMoving(this, false);
					}
				}
			}
		}
	}

	MovementIgnoredActors.Reset();

	Super::EndPlay(EndPlayReason);
}

void AArrowProjectile::LaunchArrow(const FVector& LaunchVelocity)
{
	if (ProjectileMovementComp)
	{
		ProjectileMovementComp->ProjectileGravityScale = FlightGravityScale;
		ProjectileMovementComp->Velocity = LaunchVelocity;
		ProjectileMovementComp->Activate();
	}
}

void AArrowProjectile::IgnoreActorForMovement(AActor* ActorToIgnore)
{
	if (!ActorToIgnore || ActorToIgnore == this)
	{
		return;
	}

	if (CollisionComp)
	{
		CollisionComp->IgnoreActorWhenMoving(ActorToIgnore, true);
	}

	MovementIgnoredActors.AddUnique(ActorToIgnore);

	TArray<UPrimitiveComponent*> PrimitiveComponents;
	ActorToIgnore->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (PrimitiveComponent)
		{
			PrimitiveComponent->IgnoreActorWhenMoving(this, true);
		}
	}
}

void AArrowProjectile::SetArrowMesh(UStaticMesh* InMesh)
{
	if (MeshComp && InMesh)
	{
		MeshComp->SetStaticMesh(InMesh);
	}
}

void AArrowProjectile::InitializeDamage(UAbilitySystemComponent* InSourceASC, AActor* InInstigatorActor, float InChargeDamageMultiplier)
{
	if (!HasAuthority())
	{
		return;
	}

	SourceASC = InSourceASC;
	InstigatorActor = InInstigatorActor;
	ChargeDamageMultiplier = FMath::Max(0.0f, InChargeDamageMultiplier);
	bHasRolledCritical = false;
	bCriticalHit = false;
	BuildDamageEffectSpecs();
}

void AArrowProjectile::SetDamageEffectSpecHandle(const FGameplayEffectSpecHandle& InDamageEffectSpecHandle)
{
	DamageEffectSpecHandles.Reset();
	if (InDamageEffectSpecHandle.IsValid())
	{
		DamageEffectSpecHandles.Add(InDamageEffectSpecHandle);
	}
}

void AArrowProjectile::SetAdditionalDamageEffectSpecHandles(const TArray<FGameplayEffectSpecHandle>& InAdditionalDamageEffectSpecHandles)
{
	DamageEffectSpecHandles.Append(InAdditionalDamageEffectSpecHandles);
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

	BuildDamageEffectSpecs();
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

	for (const TWeakObjectPtr<AActor>& IgnoredActorPtr : MovementIgnoredActors)
	{
		if (IgnoredActorPtr.Get() == OtherActor)
		{
			return true;
		}
	}

	return false;
}

void AArrowProjectile::BuildDamageEffectSpecs()
{
	if (!HasAuthority() || !SourceASC)
	{
		return;
	}

	if (!bHasRolledCritical)
	{
		bCriticalHit = DamageData.CritChance > 0.0f && FMath::FRand() <= DamageData.CritChance;
		bHasRolledCritical = true;
	}

	const float CriticalDamageMultiplier = bCriticalHit ? DamageData.CritMultiplier : 1.0f;

	if (DamageEffectSpecHandles.Num() == 0)
	{
		for (const FArrowDamageEffect& DamageEffect : DamageData.DamageEffects)
		{
			if (!DamageEffect.DamageEffectClass)
			{
				continue;
			}

			const float ChargeMultiplier = DamageEffect.bScaleWithCharge ? ChargeDamageMultiplier : 1.0f;
			const float CritMultiplier = DamageEffect.bCanCrit ? CriticalDamageMultiplier : 1.0f;
			const float FinalDamage = DamageEffect.BaseDamage * ChargeMultiplier * CritMultiplier;

			FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
			ContextHandle.AddInstigator(InstigatorActor.Get(), this);
			ContextHandle.AddSourceObject(this);

			FGameplayEffectSpecHandle DamageSpecHandle = SourceASC->MakeOutgoingSpec(
				DamageEffect.DamageEffectClass,
				FMath::Max(1, DamageEffect.EffectLevel),
				ContextHandle);

			if (DamageSpecHandle.IsValid())
			{
				DamageSpecHandle.Data->SetSetByCallerMagnitude(Data_Damage, FMath::Max(0.0f, FinalDamage));
				DamageEffectSpecHandles.Add(DamageSpecHandle);
			}
		}
	}

	if (StatusEffectSpecHandles.Num() == 0)
	{
		StatusEffectRefreshGrantedTags.Reset();

		for (const FArrowStatusEffect& StatusEffect : DamageData.StatusEffects)
		{
			if (!StatusEffect.StatusEffectClass)
			{
				continue;
			}

			FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
			ContextHandle.AddInstigator(InstigatorActor.Get(), this);
			ContextHandle.AddSourceObject(this);

			FGameplayEffectSpecHandle StatusSpecHandle = SourceASC->MakeOutgoingSpec(
				StatusEffect.StatusEffectClass,
				FMath::Max(1, StatusEffect.EffectLevel),
				ContextHandle);

			if (StatusSpecHandle.IsValid())
			{
				StatusEffectSpecHandles.Add(StatusSpecHandle);
				StatusEffectRefreshGrantedTags.Add(StatusEffect.RefreshGrantedTag);
			}
		}

		for (const TSubclassOf<UGameplayEffect>& StatusEffectClass : DamageData.StatusEffectClasses)
		{
			if (!StatusEffectClass)
			{
				continue;
			}

			FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
			ContextHandle.AddInstigator(InstigatorActor.Get(), this);
			ContextHandle.AddSourceObject(this);

			FGameplayEffectSpecHandle StatusSpecHandle = SourceASC->MakeOutgoingSpec(
				StatusEffectClass,
				1.0f,
				ContextHandle);

			if (StatusSpecHandle.IsValid())
			{
				StatusEffectSpecHandles.Add(StatusSpecHandle);
				StatusEffectRefreshGrantedTags.Add(FGameplayTag());
			}
		}
	}
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

	for (const FGameplayEffectSpecHandle& DamageSpecHandle : DamageEffectSpecHandles)
	{
		if (DamageSpecHandle.IsValid() && DamageSpecHandle.Data.IsValid())
		{
			TargetASC->ApplyGameplayEffectSpecToSelf(*DamageSpecHandle.Data.Get());
		}
	}

	for (int32 StatusEffectIndex = 0; StatusEffectIndex < StatusEffectSpecHandles.Num(); ++StatusEffectIndex)
	{
		const FGameplayEffectSpecHandle& StatusSpecHandle = StatusEffectSpecHandles[StatusEffectIndex];
		if (StatusSpecHandle.IsValid() && StatusSpecHandle.Data.IsValid())
		{
			const FGameplayTag RefreshGrantedTag = StatusEffectRefreshGrantedTags.IsValidIndex(StatusEffectIndex)
				? StatusEffectRefreshGrantedTags[StatusEffectIndex]
				: FGameplayTag();

			UStatusEffectLibrary::ApplyDurationDamageEffectSpecToTarget(TargetASC, StatusSpecHandle, RefreshGrantedTag);
		}
	}
}
