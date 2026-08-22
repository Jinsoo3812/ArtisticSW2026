#include "Item/Projectiles/ArrowProjectile.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GAS/SWCombatEffectContextLibrary.h"
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
	DamageEffectSpecHandles.Reset();
	StatusEffectSpecHandles.Reset();
	AppliedActors.Reset();
	BuildStatusEffectSpecs();
}

void AArrowProjectile::InitializeStrengthDamage(
	UAbilitySystemComponent* InSourceASC,
	AActor* InInstigatorActor,
	const FGameplayEffectSpecHandle& InDirectDamageSpec)
{
	if (!HasAuthority())
	{
		return;
	}

	SourceASC = InSourceASC;
	InstigatorActor = InInstigatorActor;
	DamageEffectSpecHandles.Reset();
	StatusEffectSpecHandles.Reset();
	StatusEffectRefreshGrantedTags.Reset();
	AppliedActors.Reset();

	if (!SourceASC)
	{
		UE_LOG(LogTemp, Warning, TEXT("AArrowProjectile::InitializeStrengthDamage: SourceASC is missing."));
		return;
	}

	if (!InDirectDamageSpec.IsValid() || !InDirectDamageSpec.Data.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("AArrowProjectile::InitializeStrengthDamage: invalid Damage Spec."));
		return;
	}

	DamageEffectSpecHandles.Add(InDirectDamageSpec);
	BuildStatusEffectSpecs();
}

TSubclassOf<UGameplayEffect> AArrowProjectile::GetDirectDamageEffectClass() const
{
	if (DamageData.DirectDamageEffectClass)
	{
		return DamageData.DirectDamageEffectClass;
	}

	for (const FArrowDamageEffect& LegacyDamageEffect : DamageData.DamageEffects)
	{
		if (LegacyDamageEffect.DamageEffectClass)
		{
			return LegacyDamageEffect.DamageEffectClass;
		}
	}

	return nullptr;
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

	if (CanApplyDamageToActor(OtherActor))
	{
		ApplyDamageToActor(OtherActor, Hit);
	}
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

bool AArrowProjectile::CanApplyDamageToActor(const AActor* OtherActor) const
{
	return IsValidDamageTarget(OtherActor);
}

bool AArrowProjectile::IsValidDamageTarget(const AActor* TargetActor) const
{
	if (!IsValid(TargetActor)
		|| TargetActor == this
		|| TargetActor == GetOwner()
		|| TargetActor == GetInstigator())
	{
		return false;
	}

	const UAbilitySystemComponent* TargetASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(TargetActor));
	if (!TargetASC)
	{
		return false;
	}

	if (!bEnableTeamDamageFiltering)
	{
		return true;
	}

	const UAbilitySystemComponent* ProjectileSourceASC = SourceASC;
	if (!ProjectileSourceASC)
	{
		AActor* SourceActor = GetInstigator() ? static_cast<AActor*>(GetInstigator()) : GetOwner();
		ProjectileSourceASC = SourceActor
			? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(SourceActor)
			: nullptr;
	}

	if (!ProjectileSourceASC)
	{
		return true;
	}

	const bool bBothPlayers = ProjectileSourceASC->HasMatchingGameplayTag(Team_Player)
		&& TargetASC->HasMatchingGameplayTag(Team_Player);
	const bool bBothEnemies = ProjectileSourceASC->HasMatchingGameplayTag(Team_Enemy)
		&& TargetASC->HasMatchingGameplayTag(Team_Enemy);
	return !bBothPlayers && !bBothEnemies;
}

void AArrowProjectile::BuildStatusEffectSpecs()
{
	if (!HasAuthority() || !SourceASC)
	{
		return;
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

			FGameplayEffectContextHandle ContextHandle =
				USWCombatEffectContextLibrary::MakeCombatEffectContext(
					SourceASC, InstigatorActor.Get(), this);

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

			FGameplayEffectContextHandle ContextHandle =
				USWCombatEffectContextLibrary::MakeCombatEffectContext(
					SourceASC, InstigatorActor.Get(), this);

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

void AArrowProjectile::ApplyDamageToActor(AActor* TargetActor, const FHitResult& HitResult)
{
	if (!TargetActor)
	{
		return;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
	if (!TargetASC)
	{
		UE_LOG(LogTemp, Warning, TEXT("AArrowProjectile::ApplyDamageToActor: TargetASC is missing for %s."), *GetNameSafe(TargetActor));
		return;
	}

	const bool bHasValidDirectDamageSpec = DamageEffectSpecHandles.ContainsByPredicate(
		[](const FGameplayEffectSpecHandle& SpecHandle)
		{
			return SpecHandle.IsValid() && SpecHandle.Data.IsValid();
		});
	if (!bHasValidDirectDamageSpec)
	{
		UE_LOG(LogTemp, Warning, TEXT("AArrowProjectile::ApplyDamageToActor: invalid Damage Spec."));
		return;
	}

	const TWeakObjectPtr<AActor> TargetActorPtr(TargetActor);
	if (AppliedActors.Contains(TargetActorPtr))
	{
		return;
	}
	AppliedActors.Add(TargetActorPtr);

	for (const FGameplayEffectSpecHandle& DamageSpecHandle : DamageEffectSpecHandles)
	{
		if (DamageSpecHandle.IsValid() && DamageSpecHandle.Data.IsValid())
		{
			FGameplayEffectSpec TargetDamageSpec(*DamageSpecHandle.Data.Get());
			USWCombatEffectContextLibrary::EnrichCombatEffectSpec(
				TargetDamageSpec,
				InstigatorActor.Get(),
				this,
				TargetActor,
				&HitResult,
				GetVelocity());
			TargetASC->ApplyGameplayEffectSpecToSelf(TargetDamageSpec);
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

			FGameplayEffectSpec TargetStatusSpec(*StatusSpecHandle.Data.Get());
			USWCombatEffectContextLibrary::EnrichCombatEffectSpec(
				TargetStatusSpec,
				InstigatorActor.Get(),
				this,
				TargetActor,
				&HitResult,
				GetVelocity());
			const FGameplayEffectSpecHandle TargetStatusSpecHandle(new FGameplayEffectSpec(TargetStatusSpec));
			UStatusEffectLibrary::ApplyDurationDamageEffectSpecToTarget(
				TargetASC, TargetStatusSpecHandle, RefreshGrantedTag);
		}
	}
}
