#include "ShipAI/Abilities/GA_EnemyShipCharge.h"

#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "Components/StaticMeshComponent.h"
#include "GASCombatLibrary.h"
#include "GAS/SWCombatEffectContextLibrary.h"
#include "GASDamageInstantGameplayEffect.h"
#include "Ship.h"
#include "ShipAI/Abilities/EnemyShipSkillMath.h"
#include "ShipAI/Abilities/EnemyShipChargeTelegraph.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "TimerManager.h"

UGA_EnemyShipCharge::UGA_EnemyShipCharge()
{
	SetNativeAbilityAndCooldownTags(GameplayAbility_EnemyShip_Charge, Cooldown_EnemyShip_Charge);
	CooldownDurationSeconds = 8.0f;
	DamageGameplayEffectClass = UGASDamageInstantGameplayEffect::StaticClass();
	ChargeTelegraphClass = AEnemyShipChargeTelegraph::StaticClass();
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
	bChargeStarted = false;

	FEnemyShipNavigationOverrideRequest Request;
	Request.MoveInput = 0.0f;
	Request.PropulsionMultiplier = 1.0f;
	Request.TurnMultiplier = FMath::Max(0.0f, ChargeTurnMultiplier);
	NavigationOverrideHandle = Navigation->AcquireOverride(this, 100, Request);
	if (!NavigationOverrideHandle.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	SpawnChargeTelegraph();
	UE_LOG(LogTemp, Log, TEXT("차지 선회 시작"));

	Ship->GetWorldTimerManager().SetTimer(
		SteeringTimerHandle,
		this,
		&UGA_EnemyShipCharge::UpdateChargeSteering,
		FMath::Max(0.01f, SteeringUpdateInterval),
		true);
	Ship->GetWorldTimerManager().SetTimer(
		AimTimeoutTimerHandle,
		this,
		&UGA_EnemyShipCharge::FinishAimByTimeout,
		FMath::Max(0.1f, MaximumAimDurationSeconds),
		false);
	UpdateChargeSteering();
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
		Ship->GetWorldTimerManager().ClearTimer(AimTimeoutTimerHandle);
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
	DestroyChargeTelegraph();

	ActiveShip.Reset();
	ActiveTarget.Reset();
	NavigationOverrideHandle.Reset();
	SteeringTimerHandle.Invalidate();
	AimTimeoutTimerHandle.Invalidate();
	DurationTimerHandle.Invalidate();
	bBoundPhysicsHit = false;
	bAddedChargingTag = false;
	bCollisionConsumed = false;
	bChargeStarted = false;
	ChargeStartLocation = FVector::ZeroVector;
	ChargeDirection = FVector::ForwardVector;

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
	AShip* HitShip = Cast<AShip>(OtherActor);
	if (bCollisionConsumed || !bChargeStarted || !Ship || !HitShip || HitShip == Ship)
	{
		return;
	}

	bCollisionConsumed = true;
	const FVector SourceVelocity = Ship->BuoyancyRoot
		? Ship->BuoyancyRoot->GetComponentVelocity()
		: Ship->GetVelocity();
	const FVector TargetVelocity = HitShip->BuoyancyRoot
		? HitShip->BuoyancyRoot->GetComponentVelocity()
		: HitShip->GetVelocity();
	const float ApproachSpeed = FEnemyShipSkillMath::CalculateApproachSpeed(
		Ship->GetActorLocation(), SourceVelocity, HitShip->GetActorLocation(), TargetVelocity);
	const float Damage = FEnemyShipSkillMath::CalculateChargeDamage(
		ApproachSpeed,
		MinimumDamageApproachSpeed,
		DamagePerApproachSpeedUnit,
		MaximumCollisionDamage);

	UAbilitySystemComponent* SourceASC = Ship->GetAbilitySystemComponent();
	UAbilitySystemComponent* TargetASC = HitShip->GetAbilitySystemComponent();
	if (IsValidPlayerTarget(HitShip) && Damage > 0.0f && SourceASC && TargetASC)
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
			FGameplayEffectSpec TargetSpec(*DamageSpec.Data.Get());
			USWCombatEffectContextLibrary::EnrichCombatEffectSpec(
				TargetSpec, Ship, Ship, HitShip, &Hit, SourceVelocity);
			TargetASC->ApplyGameplayEffectSpecToSelf(TargetSpec);
			const float CurrentHealth = TargetASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute());
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("EnemyShip Charge: Hit Ship %s! Dealt %f damage. Current Health: %f"),
				*HitShip->GetName(),
				Damage,
				CurrentHealth);
		}
	}

	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

void UGA_EnemyShipCharge::UpdateChargeSteering()
{
	AEnemyShip* Ship = ActiveShip.Get();
	AShip* Target = ActiveTarget.Get();
	UEnemyShipNavigationComponent* Navigation = Ship ? Ship->GetNavigationComponent() : nullptr;
	if (!Ship || Ship->IsDeathHandled() || !Navigation
		|| (!bChargeStarted && !IsValidPlayerTarget(Target)))
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	if (bChargeStarted && HasReachedChargeEndpoint(
		ChargeStartLocation,
		ChargeDirection,
		ChargeDistance,
		Ship->GetActorLocation(),
		ChargeEndpointAcceptanceRadius))
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
		return;
	}

	FVector DesiredDirection = bChargeStarted
		? ChargeDirection
		: Target->GetActorLocation() - Ship->GetActorLocation();
	DesiredDirection.Z = 0.0f;
	if (!DesiredDirection.Normalize())
	{
		DesiredDirection = Ship->GetActorForwardVector().GetSafeNormal2D();
	}
	FVector Forward = Ship->GetActorForwardVector().GetSafeNormal2D();
	FVector Right = Ship->GetActorRightVector().GetSafeNormal2D();
	const float SignedAngle = FMath::Atan2(
		FVector::DotProduct(Right, DesiredDirection),
		FVector::DotProduct(Forward, DesiredDirection));

	FEnemyShipNavigationOverrideRequest Request;
	Request.MoveInput = bChargeStarted ? 1.0f : 0.0f;
	Request.TurnInput = FMath::Clamp(
		SignedAngle * FMath::Max(0.0f, SteeringResponsiveness),
		-FMath::Clamp(MaximumTurnInput, 0.0f, 1.0f),
		FMath::Clamp(MaximumTurnInput, 0.0f, 1.0f));
	Request.PropulsionMultiplier = bChargeStarted
		? FMath::Max(1.0f, ChargePropulsionMultiplier)
		: 1.0f;
	Request.TurnMultiplier = FMath::Max(0.0f, ChargeTurnMultiplier);
	if (!Navigation->UpdateOverride(NavigationOverrideHandle, Request))
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}
	if (!bChargeStarted)
	{
		UpdateChargeTelegraph();
	}

	if (!bChargeStarted
		&& FMath::Abs(FMath::RadiansToDegrees(SignedAngle)) <= FMath::Max(0.0f, AimAlignmentToleranceDegrees))
	{
		BeginCharge();
	}
}

void UGA_EnemyShipCharge::BeginCharge()
{
	AEnemyShip* Ship = ActiveShip.Get();
	if (bChargeStarted || !Ship || !Ship->BuoyancyRoot)
	{
		return;
	}

	bChargeStarted = true;
	ChargeStartLocation = Ship->GetActorLocation();
	ChargeDirection = Ship->GetActorForwardVector().GetSafeNormal2D();
	if (ChargeDirection.IsNearlyZero())
	{
		ChargeDirection = FVector::ForwardVector;
	}
	Ship->GetWorldTimerManager().ClearTimer(AimTimeoutTimerHandle);
	DestroyChargeTelegraph();
	UE_LOG(LogTemp, Log, TEXT("차지 돌진"));

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

	if (ChargeFailsafeDurationSeconds > KINDA_SMALL_NUMBER)
	{
		Ship->GetWorldTimerManager().SetTimer(
			DurationTimerHandle,
			this,
			&UGA_EnemyShipCharge::FinishChargeByTimeout,
			ChargeFailsafeDurationSeconds,
			false);
	}
	UpdateChargeSteering();
}

void UGA_EnemyShipCharge::FinishAimByTimeout()
{
	BeginCharge();
}

void UGA_EnemyShipCharge::FinishChargeByTimeout()
{
	UE_LOG(LogTemp, Warning, TEXT("EnemyShip Charge: failsafe timeout before reaching endpoint"));
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

bool UGA_EnemyShipCharge::HasReachedChargeEndpoint(
	const FVector& Start,
	const FVector& Direction,
	float Distance,
	const FVector& CurrentLocation,
	float AcceptanceRadius)
{
	const FVector Direction2D = Direction.GetSafeNormal2D();
	if (Direction2D.IsNearlyZero())
	{
		return true;
	}
	const float RequiredProgress = FMath::Max(0.0f, Distance - FMath::Max(0.0f, AcceptanceRadius));
	FVector Travel = CurrentLocation - Start;
	Travel.Z = 0.0f;
	const float Progress = FVector::DotProduct(Travel, Direction2D);
	return Progress >= RequiredProgress;
}

void UGA_EnemyShipCharge::SpawnChargeTelegraph()
{
	AEnemyShip* Ship = ActiveShip.Get();
	if (!Ship || !Ship->HasAuthority() || !ChargeTelegraphClass || !Ship->GetWorld())
	{
		return;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = Ship;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEnemyShipChargeTelegraph* Telegraph = Ship->GetWorld()->SpawnActor<AEnemyShipChargeTelegraph>(
		ChargeTelegraphClass, Ship->GetActorTransform(), SpawnParameters);
	if (Telegraph)
	{
		ChargeTelegraphActor = Telegraph;
		Telegraph->InitializeTelegraph(
			Ship->GetActorLocation(),
			Ship->GetActorForwardVector(),
			ChargeDistance,
			ChargeTelegraphWidth,
			ChargeTelegraphWorldZ);
	}
}

void UGA_EnemyShipCharge::UpdateChargeTelegraph()
{
	AEnemyShip* Ship = ActiveShip.Get();
	if (Ship)
	{
		if (AEnemyShipChargeTelegraph* Telegraph = ChargeTelegraphActor.Get())
		{
			Telegraph->UpdateTelegraph(Ship->GetActorLocation(), Ship->GetActorForwardVector());
		}
	}
}

void UGA_EnemyShipCharge::DestroyChargeTelegraph()
{
	if (AEnemyShipChargeTelegraph* Telegraph = ChargeTelegraphActor.Get())
	{
		Telegraph->Destroy();
	}
	ChargeTelegraphActor.Reset();
}

bool UGA_EnemyShipCharge::IsValidPlayerTarget(const AShip* Candidate) const
{
	return IsValid(Candidate)
		&& !Candidate->IsEnemyShipForEffects()
		&& Candidate->ActorHasTag(TEXT("Player"))
		&& !Candidate->ActorHasTag(TEXT("Enemy"));
}
