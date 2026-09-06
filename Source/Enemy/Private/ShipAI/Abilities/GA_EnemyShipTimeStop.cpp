#include "ShipAI/Abilities/GA_EnemyShipTimeStop.h"

#include "BaseGameplayTags.h"
#include "Cannon.h"
#include "Components/StaticMeshComponent.h"
#include "Ship.h"
#include "ShipAI/Abilities/EnemyShipTimeStopAimLine.h"
#include "ShipAI/Abilities/EnemyShipTimeStopField.h"
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
		|| !IsValidPlayerTarget(Target) || !FieldClass || !AimLineClass
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
	FixedTargetPoint = TargetLocation;
	FixedLaunchDirection = (FixedTargetPoint - FixedLineStart).GetSafeNormal();
	if (FixedLaunchDirection.IsNearlyZero())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	FixedLineEnd = ResolveFixedLineEnd(FixedLineStart, Target, AimLineMaximumDistance);

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
	AimLineActor->InitializeAimLineFromCannon(
		Cannon,
		Target,
		AimLineMaximumDistance,
		AimLineTraceIntervalSeconds);
	UpdateChargeAiming();
	if (!IsActive())
	{
		return;
	}
	Ship->GetWorldTimerManager().SetTimer(
		AimUpdateTimerHandle, this, &UGA_EnemyShipTimeStop::UpdateChargeAiming,
		FMath::Max(0.01f, AimUpdateIntervalSeconds), true);

	UE_LOG(LogTemp, Warning,
		TEXT("[EnemyShipTimeStop] Charge started. Ship=%s Target=%s Cannon=%s Duration=%.2fs Start=%s End=%s"),
		*GetNameSafe(Ship), *GetNameSafe(Target), *GetNameSafe(Cannon),
		FMath::Max(0.0f, ChargeDurationSeconds),
		*FixedLineStart.ToCompactString(), *FixedLineEnd.ToCompactString());

	if (ChargeDurationSeconds <= KINDA_SMALL_NUMBER)
	{
		ConfirmAimAndBeginCharge();
		return;
	}
	Ship->GetWorldTimerManager().SetTimer(
		ChargeTimerHandle, this, &UGA_EnemyShipTimeStop::ConfirmAimAndBeginCharge,
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
		Ship->GetWorldTimerManager().ClearTimer(AimUpdateTimerHandle);
	}
	if (AEnemyShipTimeStopAimLine* Line = AimLineActor.Get())
	{
		Line->Destroy();
	}

	ActiveShip.Reset();
	ActiveTarget.Reset();
	SelectedCannon.Reset();
	AimLineActor.Reset();
	ChargeTimerHandle.Invalidate();
	AimUpdateTimerHandle.Invalidate();
	FixedLineStart = FVector::ZeroVector;
	FixedLineEnd = FVector::ZeroVector;
	FixedTargetPoint = FVector::ZeroVector;
	FixedLaunchDirection = FVector::ForwardVector;
	ConfirmedShotDistance = 0.0f;
	bAimLocked = false;
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

void UGA_EnemyShipTimeStop::ConfirmAimAndBeginCharge()
{
	AEnemyShip* Ship = ActiveShip.Get();
	ACannon* Cannon = SelectedCannon.Get();
	AShip* Target = ActiveTarget.Get();
	if (!Ship || !Ship->HasAuthority() || Ship->IsDeathHandled() || !Cannon
		|| !IsValidPlayerTarget(Target))
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}
	FixedLineStart = Cannon->GetProjectileMuzzleTransform().GetLocation();
	FixedTargetPoint = Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetComponentLocation()
		: Target->GetActorLocation();
	FixedLaunchDirection = (FixedTargetPoint - FixedLineStart).GetSafeNormal();
	if (FixedLaunchDirection.IsNearlyZero())
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}
	const FVector ConfirmedLineEnd = AEnemyShipTimeStopAimLine::ResolveClippedLineEnd(
		FixedLineStart,
		FixedLaunchDirection,
		Target,
		AimLineMaximumDistance);
	const float ClippedDistance = FVector::Distance(FixedLineStart, ConfirmedLineEnd);
	ConfirmedShotDistance = ClippedDistance < AimLineMaximumDistance - 1.0f
		? FMath::Max(1.0f, ClippedDistance)
		: FMath::Max(1.0f, FVector::Distance(FixedLineStart, FixedTargetPoint));
	bAimLocked = true;
	if (AEnemyShipTimeStopAimLine* Line = AimLineActor.Get())
	{
		Line->LockAimTargetPoint(FixedTargetPoint);
		Line->BeginLockedCharge(ChargingEffect, ChargingEffectScale,
			ChargingEffectLifetimeScale, ChargingEffectPlaybackSpeed);
	}
	UpdateChargeAiming();

	UE_LOG(LogTemp, Warning,
		TEXT("[EnemyShipTimeStop] Aim locked. Ship=%s TargetPoint=%s Distance=%.1f Charge=%.2fs"),
		*GetNameSafe(Ship), *FixedTargetPoint.ToCompactString(), ConfirmedShotDistance,
		FMath::Max(0.0f, LockedChargeDurationSeconds));
	if (LockedChargeDurationSeconds <= KINDA_SMALL_NUMBER)
	{
		FireInstantHit();
		return;
	}
	Ship->GetWorldTimerManager().SetTimer(
		ChargeTimerHandle,
		this,
		&UGA_EnemyShipTimeStop::FireInstantHit,
		FMath::Max(0.01f, LockedChargeDurationSeconds),
		false);
}

void UGA_EnemyShipTimeStop::FireInstantHit()
{
	AEnemyShip* Ship = ActiveShip.Get();
	ACannon* Cannon = SelectedCannon.Get();
	AShip* Target = ActiveTarget.Get();
	AEnemyShipTimeStopAimLine* Line = AimLineActor.Get();
	if (!Ship || !Ship->HasAuthority() || Ship->IsDeathHandled() || !Cannon
		|| !IsValidPlayerTarget(Target) || !Line || !bAimLocked)
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	const FVector ShotStart = Cannon->GetProjectileMuzzleTransform().GetLocation();
	const float ShotDistance = FMath::Max(
		1.0f,
		ConfirmedShotDistance * FMath::Max(1.0f, MissDistanceMultiplier));
	const FVector ShotDirection = (FixedTargetPoint - ShotStart).GetSafeNormal();
	if (ShotDirection.IsNearlyZero())
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}
	const FVector MissEnd = ShotStart + ShotDirection * ShotDistance;
	FHitResult Hit;
	const bool bHitPlayer = Target->ShipDamageMesh
		&& Target->ShipDamageMesh->LineTraceComponent(
			Hit,
			ShotStart,
			MissEnd,
			FCollisionQueryParams());
	const FVector ShotEnd = bHitPlayer ? Hit.ImpactPoint : MissEnd;

	if (bHitPlayer && FieldClass && Ship->GetWorld())
	{
		FActorSpawnParameters Params;
		Params.Owner = Ship;
		Params.Instigator = Ship;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AEnemyShipTimeStopField* Field = Ship->GetWorld()->SpawnActor<AEnemyShipTimeStopField>(
			FieldClass,
			ShotEnd,
			FRotator::ZeroRotator,
			Params))
		{
			Field->InitializeTimeStop(EffectRadius, TimeStopDurationSeconds);
		}
	}
	Line->PlayInstantHitEffects(
		InstantHitTrailEffect,
		ExplosionEffect,
		ShotStart,
		ShotEnd,
		bHitPlayer,
		InstantHitTrailEffectScale,
		InstantHitTrailLifetimeSeconds,
		InstantHitTrailPlaybackSpeed,
		ExplosionEffectScale,
		ExplosionEffectLifetimeScale,
		ExplosionEffectPlaybackSpeed,
		FMath::Max(InstantHitPresentationLifetime, InstantHitTrailLifetimeSeconds));
	AimLineActor.Reset();

	UE_LOG(LogTemp, Warning,
		TEXT("[EnemyShipTimeStop] Instant hit fired. Ship=%s Cannon=%s HitPlayer=%s Start=%s End=%s"),
		*GetNameSafe(Ship), *GetNameSafe(Cannon), bHitPlayer ? TEXT("true") : TEXT("false"),
		*ShotStart.ToCompactString(), *ShotEnd.ToCompactString());
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

void UGA_EnemyShipTimeStop::UpdateChargeAiming()
{
	AEnemyShip* Ship = ActiveShip.Get();
	ACannon* Cannon = SelectedCannon.Get();
	AShip* Target = ActiveTarget.Get();
	if (!Ship || Ship->IsDeathHandled() || !Cannon || !IsValidPlayerTarget(Target))
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	const FVector MuzzleLocation = Cannon->GetProjectileMuzzleTransform().GetLocation();
	if (!bAimLocked)
	{
		FixedTargetPoint = Target->BuoyancyRoot
			? Target->BuoyancyRoot->GetComponentLocation()
			: Target->GetActorLocation();
	}
	const FVector AimDirection = (FixedTargetPoint - MuzzleLocation).GetSafeNormal();
	if (AimDirection.IsNearlyZero())
	{
		return;
	}
	const FVector LocalDirection = Cannon->GetActorTransform()
		.InverseTransformVectorNoScale(AimDirection);
	const FRotator LocalRotation = LocalDirection.Rotation();
	const float LocalYaw = FMath::UnwindDegrees(LocalRotation.Yaw);
	Cannon->SetAIAimRotation(LocalRotation.Pitch, LocalYaw);
}

bool UGA_EnemyShipTimeStop::IsValidPlayerTarget(const AShip* Candidate) const
{
	return IsValid(Candidate)
		&& !Candidate->IsEnemyShipForEffects()
		&& Candidate->ActorHasTag(TEXT("Player"))
		&& !Candidate->ActorHasTag(TEXT("Enemy"));
}
