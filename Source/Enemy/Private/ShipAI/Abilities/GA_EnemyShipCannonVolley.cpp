#include "ShipAI/Abilities/GA_EnemyShipCannonVolley.h"

#include "BaseGameplayTags.h"
#include "Cannon.h"
#include "Components/StaticMeshComponent.h"
#include "Ship.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipArchetypeData.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "HAL/IConsoleManager.h"

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
	for (const ACannon* Cannon : Ship->GetMountedCannons())
	{
		if (IsValid(Cannon) && Cannon->CanFireCannon())
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
	int32 FiredCount = 0;
	for (ACannon* Cannon : Cannons)
	{
		FVector ShotDirection;
		float ShotSpeed = 0.0f;
		if (Cannon->CanFireCannon()
			&& BuildShotSolution(Cannon, Target, Ship, ShotDirection, ShotSpeed)
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
	const AEnemyShip* Ship,
	FVector& OutDirection,
	float& OutProjectileSpeed) const
{
	OutDirection = FVector::ZeroVector;
	OutProjectileSpeed = 0.0f;
	if (!Cannon || !Target || !Ship || !Ship->EnemyShipArchetype)
	{
		return false;
	}

	const FEnemyShipCannonAimProfile& AimProfile = Ship->EnemyShipArchetype->CannonAimProfile;
	const float FlightTime = FMath::Max(0.05f, AimProfile.ProjectileFlightTime);
	const UWorld* World = Cannon->GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector Start = Cannon->GetProjectileMuzzleTransform().GetLocation();
	const FVector CurrentTargetPoint = Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetComponentLocation()
		: Target->GetActorLocation();
	const FVector TargetVelocity = Target->GetVelocity();
	FVector LaunchVelocity;
	if (!CalculateLaunchVelocity(
		Start,
		CurrentTargetPoint,
		TargetVelocity,
		World->GetGravityZ(),
		AimProfile,
		LaunchVelocity))
	{
		return false;
	}

	OutProjectileSpeed = LaunchVelocity.Size();
	OutDirection = LaunchVelocity.GetSafeNormal();
	if (const IConsoleVariable* Diagnostics =
		IConsoleManager::Get().FindConsoleVariable(TEXT("sw.ShipBalanceDiagnostics"));
		Diagnostics && Diagnostics->GetInt() != 0)
	{
		FVector TrackedVelocity(TargetVelocity.X, TargetVelocity.Y, 0.0f);
		TrackedVelocity = TrackedVelocity.GetClampedToMaxSize(
			FMath::Max(0.0f, AimProfile.TrackableTargetSpeed));
		const float StraightResidual =
			FVector::Dist2D(FVector::ZeroVector, TargetVelocity - TrackedVelocity) * FlightTime;
		const UPrimitiveComponent* TargetRoot = Target->BuoyancyRoot;
		const float AngularSpeedDeg = TargetRoot
			? FMath::Abs(TargetRoot->GetPhysicsAngularVelocityInDegrees().Z)
			: 0.0f;
		const TCHAR* InputLabel = Target->GetCurrentMoveInput() > 0.9f
			? (Target->GetCurrentTurnInput() > 0.9f ? TEXT("WD")
				: Target->GetCurrentTurnInput() < -0.9f ? TEXT("WA") : TEXT("W"))
			: TEXT("OTHER");
		const FRotator LocalAim = Cannon->GetActorTransform()
			.InverseTransformVectorNoScale(OutDirection).Rotation();
		UE_LOG(LogTemp, Display,
			TEXT("[CANNON-BALANCE] Enemy=%s PlayerRow=%s Input=%s Distance=%.1f FlightTime=%.2f TrackableSpeed=%.1f PlayerSpeed=%.1f AngularSpeedDeg=%.2f StraightResidual=%.1f LaunchSpeed=%.1f AimPitch=%.1f AimYaw=%.1f AimAllowed=%s"),
			*GetNameSafe(Ship), *Target->GetShipStatRowName().ToString(), InputLabel,
			FVector::Dist2D(Start, CurrentTargetPoint), FlightTime,
			AimProfile.TrackableTargetSpeed, TargetVelocity.Size2D(), AngularSpeedDeg,
			StraightResidual, OutProjectileSpeed, LocalAim.Pitch,
			FMath::UnwindDegrees(LocalAim.Yaw),
			Cannon->CanAIAimAtWorldDirection(OutDirection) ? TEXT("true") : TEXT("false"));
	}
	return Cannon->CanAIAimAtWorldDirection(OutDirection);
}

bool UGA_EnemyShipCannonVolley::CalculateLaunchVelocity(
	const FVector& Start,
	const FVector& CurrentTargetPoint,
	const FVector& TargetVelocity,
	float GravityZ,
	const FEnemyShipCannonAimProfile& AimProfile,
	FVector& OutLaunchVelocity)
{
	OutLaunchVelocity = FVector::ZeroVector;
	const float FlightTime = FMath::Max(0.05f, AimProfile.ProjectileFlightTime);
	FVector TrackedVelocity(TargetVelocity.X, TargetVelocity.Y, 0.0f);
	TrackedVelocity = TrackedVelocity.GetClampedToMaxSize(
		FMath::Max(0.0f, AimProfile.TrackableTargetSpeed));
	const FVector PredictedTargetPoint = CurrentTargetPoint + TrackedVelocity * FlightTime;
	const FVector Gravity(0.0f, 0.0f, GravityZ);
	OutLaunchVelocity =
		(PredictedTargetPoint - Start - 0.5f * Gravity * FlightTime * FlightTime) / FlightTime;
	return OutLaunchVelocity.SizeSquared() >= 1.0f;
}

bool UGA_EnemyShipCannonVolley::IsValidPlayerTarget(const AShip* Candidate) const
{
	return IsValid(Candidate)
		&& !Candidate->IsEnemyShipForEffects()
		&& Candidate->ActorHasTag(TEXT("Player"))
		&& !Candidate->ActorHasTag(TEXT("Enemy"));
}
