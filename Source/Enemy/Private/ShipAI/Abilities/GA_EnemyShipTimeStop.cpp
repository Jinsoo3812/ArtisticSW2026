#include "ShipAI/Abilities/GA_EnemyShipTimeStop.h"

#include "BaseGameplayTags.h"
#include "Cannon.h"
#include "Components/StaticMeshComponent.h"
#include "Ship.h"
#include "ShipAI/Abilities/EnemyShipTimeStopAimLine.h"
#include "ShipAI/Abilities/EnemyShipTimeStopField.h"
#include "ShipAI/Abilities/EnemyShipTimeStopProjectile.h"
#include "ShipAI/Abilities/GA_EnemyShipLaunchTorpedo.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "TimerManager.h"

UGA_EnemyShipTimeStop::UGA_EnemyShipTimeStop()
{
	SetNativeAbilityAndCooldownTags(
		GameplayAbility_EnemyShip_TimeStop,
		Cooldown_EnemyShip_TimeStop);
	CooldownDurationSeconds = 10.0f;
	ProjectileClass = AEnemyShipTimeStopProjectile::StaticClass();
	FieldClass = AEnemyShipTimeStopField::StaticClass();
	AimLineClass = AEnemyShipTimeStopAimLine::StaticClass();
}

FGameplayTag UGA_EnemyShipTimeStop::GetTimeStopAbilityTag()
{
	return GameplayAbility_EnemyShip_TimeStop;
}

void UGA_EnemyShipTimeStop::ActivateAbility(
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
		|| !IsValidPlayerTarget(Target) || !ProjectileClass || !FieldClass || !AimLineClass
		|| !Ship->GetWorld())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	Ship->RefreshMountedCannons();
	const FVector TargetLocation = Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetComponentLocation()
		: Target->GetActorLocation();
	ACannon* Cannon = UGA_EnemyShipLaunchTorpedo::SelectClosestCannon(Ship, TargetLocation);
	if (!Cannon || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActiveShip = Ship;
	ActiveTarget = Target;
	SelectedCannon = Cannon;
	FixedLineStart = Cannon->GetProjectileMuzzleTransform().GetLocation();
	FixedLaunchDirection = (TargetLocation - FixedLineStart).GetSafeNormal();
	if (FixedLaunchDirection.IsNearlyZero())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	FixedLineEnd = ResolveFixedLineEnd(FixedLineStart, Target, AimLineMaximumDistance);

	const FVector LocalDirection = Cannon->GetActorTransform()
		.InverseTransformVectorNoScale(FixedLaunchDirection);
	const FRotator AimRotation = LocalDirection.Rotation();
	Cannon->SetAIAimRotation(AimRotation.Pitch, FMath::UnwindDegrees(AimRotation.Yaw));

	FEnemyShipNavigationOverrideRequest Request;
	Request.Mode = EEnemyShipNavigationOverrideMode::StopMovement;
	NavigationOverrideHandle = Navigation->AcquireOverride(this, 100, Request);
	if (!NavigationOverrideHandle.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FActorSpawnParameters LineParams;
	LineParams.Owner = Ship;
	LineParams.Instigator = Ship;
	LineParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AimLineActor = Ship->GetWorld()->SpawnActor<AEnemyShipTimeStopAimLine>(
		AimLineClass, FixedLineStart, FixedLaunchDirection.Rotation(), LineParams);
	if (!AimLineActor.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	AimLineActor->InitializeAimLine(
		FixedLineStart,
		FixedLaunchDirection,
		Target,
		AimLineMaximumDistance,
		AimLineTraceIntervalSeconds);

	UE_LOG(LogTemp, Warning,
		TEXT("[EnemyShipTimeStop] Charge started. Ship=%s Target=%s Cannon=%s Duration=%.2fs Start=%s End=%s"),
		*GetNameSafe(Ship), *GetNameSafe(Target), *GetNameSafe(Cannon),
		FMath::Max(0.0f, ChargeDurationSeconds),
		*FixedLineStart.ToCompactString(), *FixedLineEnd.ToCompactString());

	if (ChargeDurationSeconds <= KINDA_SMALL_NUMBER)
	{
		FireTimeStopProjectile();
		return;
	}
	Ship->GetWorldTimerManager().SetTimer(
		ChargeTimerHandle, this, &UGA_EnemyShipTimeStop::FireTimeStopProjectile,
		FMath::Max(0.01f, ChargeDurationSeconds), false);
}

void UGA_EnemyShipTimeStop::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (AEnemyShip* Ship = ActiveShip.Get())
	{
		Ship->GetWorldTimerManager().ClearTimer(ChargeTimerHandle);
		if (UEnemyShipNavigationComponent* Navigation = Ship->GetNavigationComponent())
		{
			Navigation->ReleaseOverride(NavigationOverrideHandle);
			Navigation->ReleaseOverridesFor(this);
		}
	}
	if (AEnemyShipTimeStopAimLine* Line = AimLineActor.Get())
	{
		Line->Destroy();
	}

	ActiveShip.Reset();
	ActiveTarget.Reset();
	SelectedCannon.Reset();
	AimLineActor.Reset();
	NavigationOverrideHandle.Reset();
	ChargeTimerHandle.Invalidate();
	FixedLineStart = FVector::ZeroVector;
	FixedLineEnd = FVector::ZeroVector;
	FixedLaunchDirection = FVector::ForwardVector;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

FVector UGA_EnemyShipTimeStop::ResolveFixedLineEnd(
	const FVector& LineStart,
	const AShip* TargetShip,
	float MaximumDistance)
{
	if (!TargetShip)
	{
		return LineStart;
	}
	const FVector TargetCenter = TargetShip->BuoyancyRoot
		? TargetShip->BuoyancyRoot->GetComponentLocation()
		: TargetShip->GetActorLocation();
	return AEnemyShipTimeStopAimLine::ResolveClippedLineEnd(
		LineStart,
		(TargetCenter - LineStart).GetSafeNormal(),
		TargetShip,
		MaximumDistance);
}

void UGA_EnemyShipTimeStop::FireTimeStopProjectile()
{
	AEnemyShip* Ship = ActiveShip.Get();
	ACannon* Cannon = SelectedCannon.Get();
	if (!Ship || !Ship->HasAuthority() || Ship->IsDeathHandled() || !Cannon
		|| !ProjectileClass || FixedLaunchDirection.IsNearlyZero())
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	FActorSpawnParameters Params;
	Params.Owner = Ship;
	Params.Instigator = Ship;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEnemyShipTimeStopProjectile* Projectile = Ship->GetWorld()->SpawnActor<AEnemyShipTimeStopProjectile>(
		ProjectileClass, FixedLineStart, FixedLaunchDirection.Rotation(), Params);
	if (!Projectile)
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}
	Projectile->InitializeTimeStopProjectile(
		Ship, FixedLaunchDirection, ProjectileSpeed, ProjectileLifetimeSeconds,
		EffectRadius, TimeStopDurationSeconds, FieldClass);

	UE_LOG(LogTemp, Warning,
		TEXT("[EnemyShipTimeStop] Fired. Ship=%s Cannon=%s Projectile=%s Direction=%s"),
		*GetNameSafe(Ship), *GetNameSafe(Cannon), *GetNameSafe(Projectile),
		*FixedLaunchDirection.ToCompactString());
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

bool UGA_EnemyShipTimeStop::IsValidPlayerTarget(const AShip* Candidate) const
{
	return IsValid(Candidate)
		&& !Candidate->IsEnemyShipForEffects()
		&& Candidate->ActorHasTag(TEXT("Player"))
		&& !Candidate->ActorHasTag(TEXT("Enemy"));
}
