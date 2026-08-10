#include "ShipAI/Abilities/GA_EnemyShipCharge.h"

#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "Components/StaticMeshComponent.h"
#include "GASCombatLibrary.h"
#include "GASDamageInstantGameplayEffect.h"
#include "Ship.h"
#include "ShipAI/Abilities/EnemyShipSkillMath.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "TimerManager.h"

UGA_EnemyShipCharge::UGA_EnemyShipCharge()
{
	SetNativeAbilityAndCooldownTags(GameplayAbility_EnemyShip_Charge, Cooldown_EnemyShip_Charge);
	CooldownDurationSeconds = 8.0f;
	DamageGameplayEffectClass = UGASDamageInstantGameplayEffect::StaticClass();
}

void UGA_EnemyShipCharge::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AEnemyShip* Ship = ActorInfo ? Cast<AEnemyShip>(ActorInfo->AvatarActor.Get()) : nullptr;
	UEnemyShipNavigationComponent* Navigation = Ship ? Ship->GetNavigationComponent() : nullptr;
	AShip* Target = Navigation ? Navigation->GetTargetShip() : nullptr;
	if (!Ship || !Ship->HasAuthority() || Ship->IsDeathHandled() || !Navigation
		|| !Ship->BuoyancyRoot || !IsValidPlayerTarget(Target) || !Target->BuoyancyRoot
		|| !DamageGameplayEffectClass)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActiveShip = Ship;
	ActiveTarget = Target;
	bCollisionConsumed = false;

	FEnemyShipNavigationOverrideRequest Request;
	Request.MoveInput = 1.0f;
	Request.PropulsionMultiplier = FMath::Max(1.0f, ChargePropulsionMultiplier);
	NavigationOverrideHandle = Navigation->AcquireOverride(this, 100, Request);
	if (!NavigationOverrideHandle.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (FBodyInstance* BodyInstance = Ship->BuoyancyRoot->GetBodyInstance())
	{
		bPreviousNotifyRigidBodyCollision = BodyInstance->bNotifyRigidBodyCollision;
	}
	Ship->BuoyancyRoot->SetNotifyRigidBodyCollision(true);
	Ship->BuoyancyRoot->OnComponentHit.AddUniqueDynamic(this, &UGA_EnemyShipCharge::HandlePhysicsRootHit);
	bBoundPhysicsHit = true;

	if (UAbilitySystemComponent* ASC = Ship->GetAbilitySystemComponent())
	{
		ASC->AddLooseGameplayTag(State_EnemyShip_Charging);
		bAddedChargingTag = true;
	}

	UpdateChargeSteering();
	Ship->GetWorldTimerManager().SetTimer(
		SteeringTimerHandle,
		this,
		&UGA_EnemyShipCharge::UpdateChargeSteering,
		FMath::Max(0.01f, SteeringUpdateInterval),
		true);
	Ship->GetWorldTimerManager().SetTimer(
		DurationTimerHandle,
		this,
		&UGA_EnemyShipCharge::FinishChargeByTimeout,
		FMath::Max(0.05f, ChargeDurationSeconds),
		false);
}

void UGA_EnemyShipCharge::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (AEnemyShip* Ship = ActiveShip.Get())
	{
		Ship->GetWorldTimerManager().ClearTimer(SteeringTimerHandle);
		Ship->GetWorldTimerManager().ClearTimer(DurationTimerHandle);
		if (bBoundPhysicsHit && Ship->BuoyancyRoot)
		{
			Ship->BuoyancyRoot->OnComponentHit.RemoveDynamic(this, &UGA_EnemyShipCharge::HandlePhysicsRootHit);
			Ship->BuoyancyRoot->SetNotifyRigidBodyCollision(bPreviousNotifyRigidBodyCollision);
		}
		if (UEnemyShipNavigationComponent* Navigation = Ship->GetNavigationComponent())
		{
			Navigation->ReleaseOverride(NavigationOverrideHandle);
			Navigation->ReleaseOverridesFor(this);
		}
		if (bAddedChargingTag)
		{
			if (UAbilitySystemComponent* ASC = Ship->GetAbilitySystemComponent())
			{
				ASC->RemoveLooseGameplayTag(State_EnemyShip_Charging);
			}
		}
	}

	ActiveShip.Reset();
	ActiveTarget.Reset();
	NavigationOverrideHandle.Reset();
	SteeringTimerHandle.Invalidate();
	DurationTimerHandle.Invalidate();
	bBoundPhysicsHit = false;
	bAddedChargingTag = false;
	bCollisionConsumed = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_EnemyShipCharge::HandlePhysicsRootHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	AEnemyShip* Ship = ActiveShip.Get();
	AShip* Target = ActiveTarget.Get();
	if (bCollisionConsumed || !Ship || !Target || OtherActor != Target
		|| OtherComponent != Target->BuoyancyRoot || !IsValidPlayerTarget(Target))
	{
		return;
	}

	bCollisionConsumed = true;
	const FVector SourceVelocity = Ship->BuoyancyRoot
		? Ship->BuoyancyRoot->GetComponentVelocity()
		: Ship->GetVelocity();
	const FVector TargetVelocity = Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetComponentVelocity()
		: Target->GetVelocity();
	const float ApproachSpeed = FEnemyShipSkillMath::CalculateApproachSpeed(
		Ship->GetActorLocation(), SourceVelocity, Target->GetActorLocation(), TargetVelocity);
	const float Damage = FEnemyShipSkillMath::CalculateChargeDamage(
		ApproachSpeed,
		MinimumDamageApproachSpeed,
		DamagePerApproachSpeedUnit,
		MaximumCollisionDamage);

	UAbilitySystemComponent* SourceASC = Ship->GetAbilitySystemComponent();
	UAbilitySystemComponent* TargetASC = Target->GetAbilitySystemComponent();
	if (Damage > 0.0f && SourceASC && TargetASC)
	{
		const FGameplayEffectSpecHandle DamageSpec = UGASCombatLibrary::MakeDamageEffectSpec(
			SourceASC,
			DamageGameplayEffectClass,
			Damage,
			Ship,
			Ship,
			GetAbilityLevel(),
			true,
			Hit);
		if (DamageSpec.IsValid() && DamageSpec.Data.IsValid())
		{
			TargetASC->ApplyGameplayEffectSpecToSelf(*DamageSpec.Data.Get());
		}
	}

	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

void UGA_EnemyShipCharge::UpdateChargeSteering()
{
	AEnemyShip* Ship = ActiveShip.Get();
	AShip* Target = ActiveTarget.Get();
	UEnemyShipNavigationComponent* Navigation = Ship ? Ship->GetNavigationComponent() : nullptr;
	if (!Ship || Ship->IsDeathHandled() || !Navigation || !IsValidPlayerTarget(Target))
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	FVector ToTarget = Target->GetActorLocation() - Ship->GetActorLocation();
	ToTarget.Z = 0.0f;
	if (!ToTarget.Normalize())
	{
		ToTarget = Ship->GetActorForwardVector();
	}
	FVector Forward = Ship->GetActorForwardVector().GetSafeNormal2D();
	FVector Right = Ship->GetActorRightVector().GetSafeNormal2D();
	const float SignedAngle = FMath::Atan2(
		FVector::DotProduct(Right, ToTarget),
		FVector::DotProduct(Forward, ToTarget));

	FEnemyShipNavigationOverrideRequest Request;
	Request.MoveInput = 1.0f;
	Request.TurnInput = FMath::Clamp(
		SignedAngle * FMath::Max(0.0f, SteeringResponsiveness),
		-FMath::Clamp(MaximumTurnInput, 0.0f, 1.0f),
		FMath::Clamp(MaximumTurnInput, 0.0f, 1.0f));
	Request.PropulsionMultiplier = FMath::Max(1.0f, ChargePropulsionMultiplier);
	if (!Navigation->UpdateOverride(NavigationOverrideHandle, Request))
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
	}
}

void UGA_EnemyShipCharge::FinishChargeByTimeout()
{
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

bool UGA_EnemyShipCharge::IsValidPlayerTarget(const AShip* Candidate) const
{
	return IsValid(Candidate)
		&& !Candidate->IsEnemyShipForEffects()
		&& Candidate->ActorHasTag(TEXT("Player"))
		&& !Candidate->ActorHasTag(TEXT("Enemy"));
}
