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
	}
	VolleyTimerHandle.Invalidate();
	ActiveShip.Reset();
	ActiveTarget.Reset();
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
	if (!Ship || !Ship->HasAuthority() || Ship->IsDeathHandled() || !IsValidPlayerTarget(Target)
		|| !TorpedoClass || !Ship->GetWorld())
	{
		return false;
	}

	// Cannon choice, muzzle position, and target point are intentionally refreshed for every shot.
	Ship->RefreshMountedCannons();
	const FVector TargetShipLocation = Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetComponentLocation()
		: Target->GetActorLocation();
	const FVector EnemyShipLocation = Ship->BuoyancyRoot
		? Ship->BuoyancyRoot->GetComponentLocation()
		: Ship->GetActorLocation();
	ACannon* Cannon = SelectClosestCannon(Ship, TargetShipLocation);
	if (!Cannon)
	{
		return false;
	}

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
