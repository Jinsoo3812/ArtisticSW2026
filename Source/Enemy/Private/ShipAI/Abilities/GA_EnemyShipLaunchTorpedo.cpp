#include "ShipAI/Abilities/GA_EnemyShipLaunchTorpedo.h"

#include "BaseGameplayTags.h"
#include "Cannon.h"
#include "Engine/World.h"
#include "Ship.h"
#include "ShipAI/Abilities/EnemyShipTorpedo.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "TimerManager.h"

UGA_EnemyShipLaunchTorpedo::UGA_EnemyShipLaunchTorpedo()
{
	SetNativeAbilityAndCooldownTags(
		GameplayAbility_EnemyShip_LaunchTorpedo,
		Cooldown_EnemyShip_LaunchTorpedo);
	CooldownDurationSeconds = 6.0f;
	TorpedoClass = AEnemyShipTorpedo::StaticClass();
}

void UGA_EnemyShipLaunchTorpedo::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AEnemyShip* Ship = ActorInfo ? Cast<AEnemyShip>(ActorInfo->AvatarActor.Get()) : nullptr;
	UEnemyShipNavigationComponent* Navigation = Ship ? Ship->GetNavigationComponent() : nullptr;
	AShip* Target = Navigation ? Navigation->GetTargetShip() : nullptr;
	if (!Ship || !Ship->HasAuthority() || Ship->IsDeathHandled() || !IsValidPlayerTarget(Target)
		|| !TorpedoClass || !Ship->GetWorld())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// One activation owns one cooldown, regardless of the number of torpedoes in the volley.
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActiveShip = Ship;
	ActiveTarget = Target;
	LaunchedTorpedoCount = 0;
	Ship->RefreshMountedCannons();
	const FVector InitialTargetLocation = Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetComponentLocation()
		: Target->GetActorLocation();
	ActiveCannon = SelectClosestCannon(Ship, InitialTargetLocation);
	if (!ActiveCannon.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FEnemyShipNavigationOverrideRequest AimRequest;
	AimRequest.MoveInput = 0.0f;
	AimRequest.TurnInput = 0.0f;
	AimRequest.PropulsionMultiplier = 1.0f;
	AimRequest.TurnMultiplier = FMath::Max(0.0f, ShipTurnMultiplier);
	NavigationOverrideHandle = Navigation->AcquireOverride(this, 100, AimRequest);
	if (!NavigationOverrideHandle.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	Ship->GetWorldTimerManager().SetTimer(
		AimTimerHandle, this, &UGA_EnemyShipLaunchTorpedo::UpdateVolleyAiming,
		FMath::Max(0.01f, AimUpdateIntervalSeconds), true);
	UpdateVolleyAiming();

	const int32 RequestedCount = FMath::Max(1, TorpedoCount);
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[EnemyShipTorpedo] 스킬 시작 Ship=%s Target=%s Count=%d Duration=%.2f Alpha=%.2f"),
		*GetNameSafe(Ship),
		*GetNameSafe(Target),
		RequestedCount,
		FMath::Max(0.0f, VolleyDurationSeconds),
		FMath::Clamp(TargetLineAlpha, 0.0f, 1.0f));
	if (!FireSingleTorpedo())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	++LaunchedTorpedoCount;

	if (LaunchedTorpedoCount >= RequestedCount)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	const float Duration = FMath::Max(0.0f, VolleyDurationSeconds);
	if (Duration <= KINDA_SMALL_NUMBER)
	{
		while (LaunchedTorpedoCount < RequestedCount)
		{
			if (!FireSingleTorpedo())
			{
				EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
				return;
			}
			++LaunchedTorpedoCount;
		}
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	// The first projectile launches immediately; the final projectile launches at Duration.
	const float LaunchInterval = Duration / static_cast<float>(RequestedCount - 1);
	Ship->GetWorldTimerManager().SetTimer(
		VolleyTimerHandle,
		this,
		&UGA_EnemyShipLaunchTorpedo::LaunchNextTorpedo,
		FMath::Max(0.001f, LaunchInterval),
		true);
}

void UGA_EnemyShipLaunchTorpedo::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (AEnemyShip* Ship = ActiveShip.Get())
	{
		Ship->GetWorldTimerManager().ClearTimer(VolleyTimerHandle);
		Ship->GetWorldTimerManager().ClearTimer(AimTimerHandle);
		if (UEnemyShipNavigationComponent* Navigation = Ship->GetNavigationComponent())
		{
			Navigation->ReleaseOverride(NavigationOverrideHandle);
			Navigation->ReleaseOverridesFor(this);
		}
	}
	VolleyTimerHandle.Invalidate();
	AimTimerHandle.Invalidate();
	ActiveShip.Reset();
	ActiveTarget.Reset();
	ActiveCannon.Reset();
	NavigationOverrideHandle.Reset();
	LaunchedTorpedoCount = 0;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_EnemyShipLaunchTorpedo::LaunchNextTorpedo()
{
	if (!FireSingleTorpedo())
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	++LaunchedTorpedoCount;
	if (LaunchedTorpedoCount >= FMath::Max(1, TorpedoCount))
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
	}
}

bool UGA_EnemyShipLaunchTorpedo::FireSingleTorpedo()
{
	AEnemyShip* Ship = ActiveShip.Get();
	AShip* Target = ActiveTarget.Get();
	ACannon* Cannon = ActiveCannon.Get();
	if (!Ship || !Ship->HasAuthority() || Ship->IsDeathHandled() || !IsValidPlayerTarget(Target)
		|| !Cannon || !TorpedoClass || !Ship->GetWorld())
	{
		return false;
	}

	// The selected cannon remains stable for the volley; muzzle and target refresh per shot.
	const FVector TargetShipLocation = Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetComponentLocation()
		: Target->GetActorLocation();
	const FVector EnemyShipLocation = Ship->BuoyancyRoot
		? Ship->BuoyancyRoot->GetComponentLocation()
		: Ship->GetActorLocation();
	const FCannonResolvedFiringStats FiringStats = Cannon->GetResolvedFiringStats();
	const FTransform MuzzleTransform = Cannon->GetProjectileMuzzleTransform();
	const FVector TargetPoint = CalculateLineTargetPoint(
		EnemyShipLocation,
		TargetShipLocation,
		TargetLineAlpha);
	const FVector LaunchDirection = (TargetPoint - MuzzleTransform.GetLocation()).GetSafeNormal();
	if (LaunchDirection.IsNearlyZero() || FiringStats.ProjectileSpeed <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector LocalLaunchDirection = Cannon->GetActorTransform()
		.InverseTransformVectorNoScale(LaunchDirection);
	const FRotator AimRotation = LocalLaunchDirection.Rotation();
	Cannon->SetAIAimRotation(AimRotation.Pitch, AimRotation.Yaw);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = Ship;
	SpawnParameters.Instigator = Ship;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEnemyShipTorpedo* Torpedo = Ship->GetWorld()->SpawnActor<AEnemyShipTorpedo>(
		TorpedoClass,
		MuzzleTransform.GetLocation(),
		LaunchDirection.Rotation(),
		SpawnParameters);
	if (!Torpedo)
	{
		return false;
	}

	const float SnapshotDamage = FMath::Max(0.0f, FiringStats.Damage)
		* FMath::Max(0.0f, TorpedoDamageMultiplier);
	Torpedo->InitializeTorpedo(
		Ship,
		Target,
		SnapshotDamage,
		FiringStats.ProjectileSpeed,
		FMath::Max(0.1f, MaximumFlightSeconds));

	return true;
}

void UGA_EnemyShipLaunchTorpedo::UpdateVolleyAiming()
{
	AEnemyShip* Ship = ActiveShip.Get();
	AShip* Target = ActiveTarget.Get();
	ACannon* Cannon = ActiveCannon.Get();
	UEnemyShipNavigationComponent* Navigation = Ship ? Ship->GetNavigationComponent() : nullptr;
	if (!Ship || Ship->IsDeathHandled() || !IsValidPlayerTarget(Target) || !Cannon || !Navigation)
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	const FVector TargetLocation = Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetComponentLocation()
		: Target->GetActorLocation();
	const FVector EnemyLocation = Ship->BuoyancyRoot
		? Ship->BuoyancyRoot->GetComponentLocation()
		: Ship->GetActorLocation();
	const FVector AimPoint = CalculateLineTargetPoint(EnemyLocation, TargetLocation, TargetLineAlpha);
	const FVector AimDirection = (AimPoint - Cannon->GetProjectileMuzzleTransform().GetLocation()).GetSafeNormal();
	if (AimDirection.IsNearlyZero())
	{
		return;
	}

	const FVector LocalDirection = Cannon->GetActorTransform()
		.InverseTransformVectorNoScale(AimDirection);
	const FRotator LocalRotation = LocalDirection.Rotation();
	const float LocalYawRadians = FMath::DegreesToRadians(FMath::UnwindDegrees(LocalRotation.Yaw));
	Cannon->SetAIAimRotation(LocalRotation.Pitch, FMath::UnwindDegrees(LocalRotation.Yaw));

	FEnemyShipNavigationOverrideRequest Request;
	Request.MoveInput = 0.0f;
	Request.TurnInput = FMath::Clamp(
		LocalYawRadians * FMath::Max(0.0f, ShipTurnResponsiveness), -1.0f, 1.0f);
	Request.PropulsionMultiplier = 1.0f;
	Request.TurnMultiplier = FMath::Max(0.0f, ShipTurnMultiplier);
	if (!Navigation->UpdateOverride(NavigationOverrideHandle, Request))
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
	}
}

ACannon* UGA_EnemyShipLaunchTorpedo::SelectClosestCannon(
	const AEnemyShip* Ship,
	const FVector& TargetLocation)
{
	if (!Ship)
	{
		return nullptr;
	}

	ACannon* Closest = nullptr;
	float ClosestDistanceSquared = TNumericLimits<float>::Max();
	for (ACannon* Cannon : Ship->GetMountedCannons())
	{
		if (!IsValid(Cannon))
		{
			continue;
		}
		const float DistanceSquared = FVector::DistSquared2D(Cannon->GetActorLocation(), TargetLocation);
		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			Closest = Cannon;
		}
	}
	return Closest;
}

FVector UGA_EnemyShipLaunchTorpedo::CalculateLineTargetPoint(
	const FVector& EnemyShipLocation,
	const FVector& PlayerShipLocation,
	float LineAlpha)
{
	return FMath::Lerp(
		EnemyShipLocation,
		PlayerShipLocation,
		FMath::Clamp(LineAlpha, 0.0f, 1.0f));
}

bool UGA_EnemyShipLaunchTorpedo::IsValidPlayerTarget(const AShip* Candidate) const
{
	return IsValid(Candidate)
		&& !Candidate->IsEnemyShipForEffects()
		&& Candidate->ActorHasTag(TEXT("Player"))
		&& !Candidate->ActorHasTag(TEXT("Enemy"));
}
