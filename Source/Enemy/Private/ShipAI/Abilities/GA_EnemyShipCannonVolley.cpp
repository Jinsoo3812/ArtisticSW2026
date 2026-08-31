#include "ShipAI/Abilities/GA_EnemyShipCannonVolley.h"

#include "BaseGameplayTags.h"
#include "Cannon.h"
#include "Components/StaticMeshComponent.h"
#include "Ship.h"
#include "ShipAI/Abilities/EnemyShipSkillMath.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ShipAI/EnemyShipPatternRuntimeComponent.h"

UGA_EnemyShipCannonVolley::UGA_EnemyShipCannonVolley()
{
	SetNativeAbilityTag(GameplayAbility_EnemyShip_CannonVolley);
	CooldownDurationSeconds = 0.0f;
}

bool UGA_EnemyShipCannonVolley::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const AEnemyShip* Ship = ActorInfo ? Cast<AEnemyShip>(ActorInfo->AvatarActor.Get()) : nullptr;
	const UEnemyShipNavigationComponent* Navigation = Ship ? Ship->GetNavigationComponent() : nullptr;
	const AShip* Target = Navigation ? Navigation->GetTargetShip() : nullptr;
	if (!Ship || !Ship->HasAuthority() || !IsValidPlayerTarget(Target))
	{
		return false;
	}
	const float PredictionStrength = ResolvePredictionStrength(Ship);
	const float MaximumProjectileSpeed = ResolveMaximumProjectileSpeed(Ship);

	for (const ACannon* Cannon : Ship->GetMountedCannons())
	{
		FVector ShotDirection;
		float ShotSpeed = 0.0f;
		if (IsValid(Cannon) && Cannon->CanFireCannon()
			&& BuildShotSolution(Cannon, Target, PredictionStrength, MaximumProjectileSpeed, ShotDirection, ShotSpeed))
		{
			return true;
		}
	}
	return false;
}

void UGA_EnemyShipCannonVolley::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AEnemyShip* Ship = ActorInfo ? Cast<AEnemyShip>(ActorInfo->AvatarActor.Get()) : nullptr;
	UEnemyShipNavigationComponent* Navigation = Ship ? Ship->GetNavigationComponent() : nullptr;
	AShip* Target = Navigation ? Navigation->GetTargetShip() : nullptr;
	if (!Ship || !Ship->HasAuthority() || !IsValidPlayerTarget(Target)
		|| !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	Ship->RefreshMountedCannons();
	TArray<ACannon*> Cannons;
	for (ACannon* Cannon : Ship->GetMountedCannons())
	{
		if (IsValid(Cannon))
		{
			Cannons.AddUnique(Cannon);
		}
	}
	const FVector TargetLocation = Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetComponentLocation()
		: Target->GetActorLocation();
	Cannons.StableSort([TargetLocation](const ACannon& A, const ACannon& B)
	{
		return FVector::DistSquared2D(A.GetActorLocation(), TargetLocation)
			< FVector::DistSquared2D(B.GetActorLocation(), TargetLocation);
	});
	const float PredictionStrength = ResolvePredictionStrength(Ship);
	const float MaximumProjectileSpeed = ResolveMaximumProjectileSpeed(Ship);

	int32 FiredCount = 0;
	for (ACannon* Cannon : Cannons)
	{
		if (FiredCount >= FMath::Max(1, MaxCannonsPerVolley))
		{
			break;
		}

		FVector ShotDirection;
		float ShotSpeed = 0.0f;
		if (Cannon->CanFireCannon()
			&& BuildShotSolution(Cannon, Target, PredictionStrength, MaximumProjectileSpeed, ShotDirection, ShotSpeed)
			&& Cannon->FireAICannonAtDirectionWithSpeed(ShotDirection, ShotSpeed))
		{
			++FiredCount;
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, FiredCount == 0);
}

bool UGA_EnemyShipCannonVolley::BuildShotSolution(
	const ACannon* Cannon,
	const AShip* Target,
	float PredictionStrength,
	float MaximumProjectileSpeed,
	FVector& OutDirection,
	float& OutProjectileSpeed) const
{
	OutDirection = FVector::ZeroVector;
	OutProjectileSpeed = 0.0f;
	if (!Cannon || !Target)
	{
		return false;
	}

	const FVector CurrentTargetPoint = Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetComponentLocation()
		: Target->GetActorLocation();
	const float ClampedPredictionStrength = FMath::Clamp(PredictionStrength, 0.0f, 1.0f);
	if (ClampedPredictionStrength <= KINDA_SMALL_NUMBER)
	{
		float FlightTime = 0.0f;
		return SolveShotToPoint(
			Cannon, CurrentTargetPoint, MaximumProjectileSpeed, OutDirection, OutProjectileSpeed, FlightTime);
	}

	// AShip already exposes the authoritative physics velocity through AActor::GetVelocity.
	// Iterating flight time and target position solves the constant-velocity intercept,
	// including the per-shot automatic projectile-speed adjustment.
	const FVector TargetVelocity = Target->GetVelocity();
	FVector PredictedTargetPoint = CurrentTargetPoint;
	for (int32 Iteration = 0; Iteration < 8; ++Iteration)
	{
		FVector IterationDirection;
		float IterationSpeed = 0.0f;
		float IterationFlightTime = 0.0f;
		if (!SolveShotToPoint(
			Cannon, PredictedTargetPoint, MaximumProjectileSpeed,
			IterationDirection, IterationSpeed, IterationFlightTime))
		{
			return false;
		}

		const FVector NextTargetPoint = CurrentTargetPoint
			+ TargetVelocity * (IterationFlightTime * ClampedPredictionStrength);
		if (FVector::DistSquared(NextTargetPoint, PredictedTargetPoint) <= 1.0f)
		{
			OutDirection = IterationDirection;
			OutProjectileSpeed = IterationSpeed;
			return true;
		}
		PredictedTargetPoint = NextTargetPoint;
	}

	float FinalFlightTime = 0.0f;
	return SolveShotToPoint(
		Cannon, PredictedTargetPoint, MaximumProjectileSpeed, OutDirection, OutProjectileSpeed, FinalFlightTime);
}

bool UGA_EnemyShipCannonVolley::SolveShotToPoint(
	const ACannon* Cannon,
	const FVector& TargetPoint,
	float MaximumProjectileSpeed,
	FVector& OutDirection,
	float& OutProjectileSpeed,
	float& OutFlightTime) const
{
	OutDirection = FVector::ZeroVector;
	OutProjectileSpeed = 0.0f;
	OutFlightTime = 0.0f;
	if (!Cannon || MaximumProjectileSpeed < 1.0f)
	{
		return false;
	}

	const UWorld* World = Cannon->GetWorld();
	const float Gravity = World ? FMath::Abs(World->GetGravityZ()) : 0.0f;
	const FVector Start = Cannon->GetProjectileMuzzleTransform().GetLocation();
	if (Gravity <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector Delta = TargetPoint - Start;
	const float HorizontalDistance = FVector(Delta.X, Delta.Y, 0.0f).Size();
	if (HorizontalDistance <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// The closed-form lower bound is the minimum launch speed for an unobstructed
	// ballistic arc. A small margin avoids a double root at the exact limit.
	const float PhysicalMinimumSpeed = FMath::Sqrt(
		Gravity * (Delta.Z + FMath::Sqrt(
			HorizontalDistance * HorizontalDistance + Delta.Z * Delta.Z)));
	const float AuthoredSpeed = FMath::Max(1.0f, Cannon->GetResolvedFiringStats().ProjectileSpeed);
	const float LowerBound = FMath::Max(AuthoredSpeed, PhysicalMinimumSpeed * 1.001f);
	const float SpeedCeiling = FMath::Max(1.0f, MaximumProjectileSpeed);
	if (LowerBound > SpeedCeiling)
	{
		return false;
	}

	auto TrySpeed = [this, Cannon, &Start, &TargetPoint, Gravity](
		float CandidateSpeed,
		FVector& CandidateDirection,
		float& CandidateFlightTime)
	{
		FVector Velocity;
		float SolvedAngle = 0.0f;
		if (!FEnemyShipSkillMath::SuggestBallisticVelocity(
			Start, TargetPoint, CandidateSpeed, Gravity, PreferredLaunchAngleDegrees,
			Velocity, CandidateFlightTime, SolvedAngle))
		{
			return false;
		}
		CandidateDirection = Velocity.GetSafeNormal();
		return !CandidateDirection.IsNearlyZero()
			&& Cannon->CanAimAtWorldDirection(CandidateDirection);
	};

	FVector CandidateDirection;
	float CandidateFlightTime = 0.0f;
	if (TrySpeed(LowerBound, CandidateDirection, CandidateFlightTime))
	{
		OutDirection = CandidateDirection;
		OutProjectileSpeed = LowerBound;
		OutFlightTime = CandidateFlightTime;
		return true;
	}

	float FailingSpeed = LowerBound;
	float PassingSpeed = LowerBound;
	bool bFoundPassingSpeed = false;
	for (int32 Attempt = 0; Attempt < 12 && PassingSpeed < SpeedCeiling; ++Attempt)
	{
		PassingSpeed = FMath::Min(PassingSpeed * 1.25f, SpeedCeiling);
		if (TrySpeed(PassingSpeed, CandidateDirection, CandidateFlightTime))
		{
			bFoundPassingSpeed = true;
			break;
		}
		FailingSpeed = PassingSpeed;
	}
	if (!bFoundPassingSpeed)
	{
		return false;
	}

	// Find the smallest aimable speed above the physical/authored lower bound.
	for (int32 Iteration = 0; Iteration < 16; ++Iteration)
	{
		const float MidSpeed = (FailingSpeed + PassingSpeed) * 0.5f;
		FVector MidDirection;
		float MidFlightTime = 0.0f;
		if (TrySpeed(MidSpeed, MidDirection, MidFlightTime))
		{
			PassingSpeed = MidSpeed;
			CandidateDirection = MidDirection;
			CandidateFlightTime = MidFlightTime;
		}
		else
		{
			FailingSpeed = MidSpeed;
		}
	}

	OutDirection = CandidateDirection;
	OutProjectileSpeed = PassingSpeed;
	OutFlightTime = CandidateFlightTime;
	return true;
}

float UGA_EnemyShipCannonVolley::ResolvePredictionStrength(const AEnemyShip* Ship) const
{
	const UEnemyShipPatternRuntimeComponent* Runtime = Ship ? Ship->GetPatternRuntimeComponent() : nullptr;
	return Runtime
		? Runtime->GetPendingTargetPredictionStrength(GameplayAbility_EnemyShip_CannonVolley)
		: 0.0f;
}

float UGA_EnemyShipCannonVolley::ResolveMaximumProjectileSpeed(const AEnemyShip* Ship) const
{
	const UEnemyShipPatternRuntimeComponent* Runtime = Ship ? Ship->GetPatternRuntimeComponent() : nullptr;
	return Runtime
		? Runtime->GetMaximumCannonballSpeed(GameplayAbility_EnemyShip_CannonVolley)
		: 0.0f;
}

bool UGA_EnemyShipCannonVolley::IsValidPlayerTarget(const AShip* Candidate) const
{
	return IsValid(Candidate)
		&& !Candidate->IsEnemyShipForEffects()
		&& Candidate->ActorHasTag(TEXT("Player"))
		&& !Candidate->ActorHasTag(TEXT("Enemy"));
}
