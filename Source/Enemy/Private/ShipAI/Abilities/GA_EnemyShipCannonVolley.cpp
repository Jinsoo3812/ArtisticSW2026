#include "ShipAI/Abilities/GA_EnemyShipCannonVolley.h"

#include "BaseGameplayTags.h"
#include "Cannon.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Ship.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipArchetypeData.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ShipAI/EnemyShipSkillModuleData.h"
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
		FCannonTargetingDebugData DebugData;
		if (Cannon->CanFireCannon()
			&& BuildShotSolution(Cannon, Target, Ship, ShotDirection, ShotSpeed, &DebugData)
			&& Cannon->FireAICannonAtDirectionWithSpeed(ShotDirection, ShotSpeed))
		{
			++FiredCount;
			DrawCannonTargetingDebug(*Cannon, *Target, *Ship, ShotDirection, ShotSpeed, DebugData);
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, FiredCount == 0);
}

bool UGA_EnemyShipCannonVolley::BuildShotSolution(
	const ACannon* Cannon,
	const AShip* Target,
	const AEnemyShip* Ship,
	FVector& OutDirection,
	float& OutProjectileSpeed,
	FCannonTargetingDebugData* OutDebugData) const
{
	OutDirection = FVector::ZeroVector;
	OutProjectileSpeed = 0.0f;
	if (!Cannon || !Target || !Ship || !Ship->EnemyShipArchetype)
	{
		return false;
	}

	const UWorld* World = Cannon->GetWorld();
	if (!World)
	{
		return false;
	}
	const float ProjectileSpeed = Cannon->GetResolvedFiringStats().ProjectileSpeed;
	const float GravityMagnitude = FMath::Abs(World->GetGravityZ());
	if (ProjectileSpeed <= KINDA_SMALL_NUMBER || GravityMagnitude <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector Start = Cannon->GetProjectileMuzzleTransform().GetLocation();
	const FVector CurrentTargetPoint = Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetComponentLocation()
		: Target->GetActorLocation();
	FVector TargetVelocity = Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetPhysicsLinearVelocity()
		: Target->GetVelocity();
	TargetVelocity.Z = 0.0f;
	FVector TargetForward = Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetForwardVector()
		: Target->GetActorForwardVector();
	TargetForward.Z = 0.0f;
	if (!TargetForward.Normalize())
	{
		TargetForward = Target->GetActorForwardVector();
		TargetForward.Z = 0.0f;
		if (!TargetForward.Normalize())
		{
			TargetForward = FVector::ForwardVector;
		}
	}
	FVector TargetRight = FVector::CrossProduct(FVector::UpVector, TargetForward).GetSafeNormal();
	const FVector ActualTargetRight = (Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetRightVector()
		: Target->GetActorRightVector()).GetSafeNormal2D();
	if (FVector::DotProduct(TargetRight, ActualTargetRight) < 0.0f)
	{
		TargetRight *= -1.0f;
	}

	const FEnemyShipCannonVolleySettings Settings = ResolveCannonVolleySettings(*Ship);
	float MaximumRange = 0.0f;
	float MaximumRangeFlightTime = 0.0f;
	if (!CalculateRangeAtElevation(ProjectileSpeed, GravityMagnitude,
		CurrentTargetPoint.Z - Start.Z, FMath::DegreesToRadians(45.0f),
		MaximumRange, MaximumRangeFlightTime))
	{
		return false;
	}
	const float CurrentDistance = FVector::Dist2D(Start, CurrentTargetPoint);
	const bool bInsideRange = CurrentDistance <= MaximumRange;
	FVector EllipseCenter = CurrentTargetPoint;
	float EstimatedFlightTime = 0.0f;
	if (bInsideRange)
	{
		float Elevation = 0.0f;
		if (CalculateLowArc(ProjectileSpeed, GravityMagnitude, CurrentDistance,
			CurrentTargetPoint.Z - Start.Z, Elevation, EstimatedFlightTime))
		{
			FVector LastValidCenter = CurrentTargetPoint;
			for (int32 Iteration = 0; Iteration < 3; ++Iteration)
			{
				FVector CandidateCenter = CurrentTargetPoint + TargetVelocity * EstimatedFlightTime;
				CandidateCenter.Z = CurrentTargetPoint.Z;
				float CandidateElevation = 0.0f;
				float CandidateFlightTime = 0.0f;
				if (!CalculateLowArc(ProjectileSpeed, GravityMagnitude,
					FVector::Dist2D(Start, CandidateCenter), CandidateCenter.Z - Start.Z,
					CandidateElevation, CandidateFlightTime))
				{
					break;
				}
				LastValidCenter = CandidateCenter;
				EstimatedFlightTime = CandidateFlightTime;
			}
			EllipseCenter = LastValidCenter;
		}
	}
	else
	{
		FVector DirectionToTarget = CurrentTargetPoint - Start;
		DirectionToTarget.Z = 0.0f;
		if (!DirectionToTarget.Normalize())
		{
			DirectionToTarget = Cannon->GetProjectileMuzzleTransform().GetUnitAxis(EAxis::X).GetSafeNormal2D();
		}
		EllipseCenter = Start + DirectionToTarget * MaximumRange;
		EllipseCenter.Z = CurrentTargetPoint.Z;
	}

	FVector DirectionToAttacker = Ship->GetActorLocation() - CurrentTargetPoint;
	DirectionToAttacker.Z = 0.0f;
	DirectionToAttacker.Normalize();
	FRandomStream RandomStream(FMath::Rand());
	bool bFacingHalf = false;
	const FVector RequestedImpactPoint = EllipseCenter + SampleImpactEllipseOffset(
		RandomStream, TargetForward, TargetRight, DirectionToAttacker, Settings, bFacingHalf);
	bool bRangeClamped = false;
	FVector ClampedImpactPoint = ClampImpactPointToRange(
		Start, RequestedImpactPoint, MaximumRange, bRangeClamped);
	ClampedImpactPoint.Z = CurrentTargetPoint.Z;

	FVector HorizontalDirection = ClampedImpactPoint - Start;
	HorizontalDirection.Z = 0.0f;
	const float HorizontalRange = HorizontalDirection.Size();
	float CalculatedElevation = 0.0f;
	float FinalFlightTime = 0.0f;
	if (HorizontalRange <= KINDA_SMALL_NUMBER)
	{
		HorizontalDirection = Cannon->GetProjectileMuzzleTransform().GetUnitAxis(EAxis::X).GetSafeNormal2D();
		CalculatedElevation = FMath::DegreesToRadians(Settings.MinimumElevationDegrees);
	}
	else if (!CalculateLowArc(ProjectileSpeed, GravityMagnitude, HorizontalRange,
		ClampedImpactPoint.Z - Start.Z, CalculatedElevation, FinalFlightTime))
	{
		return false;
	}
	const float FinalElevation = FMath::DegreesToRadians(FMath::Clamp(
		FMath::RadiansToDegrees(CalculatedElevation), Settings.MinimumElevationDegrees, 45.0f));
	OutDirection = HorizontalDirection.GetSafeNormal() * FMath::Cos(FinalElevation)
		+ FVector::UpVector * FMath::Sin(FinalElevation);
	OutProjectileSpeed = ProjectileSpeed;
	const bool bAimAllowed = Cannon->CanAIAimAtWorldDirection(OutDirection);
	if (OutDebugData)
	{
		OutDebugData->Start = Start;
		OutDebugData->CurrentTargetPoint = CurrentTargetPoint;
		OutDebugData->TargetVelocity = TargetVelocity;
		OutDebugData->TargetForward = TargetForward;
		OutDebugData->TargetRight = TargetRight;
		OutDebugData->EllipseCenter = EllipseCenter;
		OutDebugData->RequestedImpactPoint = RequestedImpactPoint;
		OutDebugData->ClampedImpactPoint = ClampedImpactPoint;
		OutDebugData->Settings = Settings;
		OutDebugData->CurrentDistance = CurrentDistance;
		OutDebugData->MaximumRange = MaximumRange;
		OutDebugData->EstimatedFlightTime = EstimatedFlightTime;
		OutDebugData->CalculatedElevation = CalculatedElevation;
		OutDebugData->FinalElevation = FinalElevation;
		OutDebugData->bInsideRange = bInsideRange;
		OutDebugData->bFacingHalf = bFacingHalf;
		OutDebugData->bRangeClamped = bRangeClamped;
	}
	return bAimAllowed;
}

void UGA_EnemyShipCannonVolley::DrawCannonTargetingDebug(
	const ACannon& Cannon, const AShip& Target, const AEnemyShip& Ship,
	const FVector& Direction, float ProjectileSpeed, const FCannonTargetingDebugData& DebugData)
{
	const IConsoleVariable* Diagnostics =
		IConsoleManager::Get().FindConsoleVariable(TEXT("sw.ShipBalanceDiagnostics"));
	UWorld* World = Cannon.GetWorld();
	if (!Diagnostics || Diagnostics->GetInt() == 0 || !World)
	{
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[ENEMY-CANNON-TARGETING] Enemy=%s Cannon=%s EnemyRow=%s PlayerRow=%s Distance=%.1f MaximumRange=%.1f Branch=%s FlightTime=%.3f PlayerVelocityXY=%s EllipseCenter=%s Half=%s Requested=%s FinalTarget=%s RangeClamped=%s CalculatedElevation=%.2f FinalElevation=%.2f LaunchSpeed=%.1f Direction=%s"),
		*GetNameSafe(&Ship), *GetNameSafe(&Cannon), *Ship.GetShipStatRowName().ToString(), *Target.GetShipStatRowName().ToString(),
		DebugData.CurrentDistance, DebugData.MaximumRange, DebugData.bInsideRange ? TEXT("Inside") : TEXT("Outside"),
		DebugData.EstimatedFlightTime, *DebugData.TargetVelocity.ToCompactString(), *DebugData.EllipseCenter.ToCompactString(),
		DebugData.bFacingHalf ? TEXT("Facing") : TEXT("Opposite"), *DebugData.RequestedImpactPoint.ToCompactString(),
		*DebugData.ClampedImpactPoint.ToCompactString(), DebugData.bRangeClamped ? TEXT("true") : TEXT("false"),
		FMath::RadiansToDegrees(DebugData.CalculatedElevation), FMath::RadiansToDegrees(DebugData.FinalElevation),
		ProjectileSpeed, *Direction.ToCompactString());

	constexpr float DebugDuration = 8.0f;
	constexpr int32 EllipseSegments = 48;
	FVector PreviousEllipsePoint = DebugData.EllipseCenter
		+ DebugData.TargetForward * DebugData.Settings.ImpactEllipseSemiMajorAxisCm;
	for (int32 SegmentIndex = 1; SegmentIndex <= EllipseSegments; ++SegmentIndex)
	{
		const float Angle = 2.0f * PI * static_cast<float>(SegmentIndex) / EllipseSegments;
		const FVector EllipsePoint = DebugData.EllipseCenter
			+ DebugData.TargetForward * (DebugData.Settings.ImpactEllipseSemiMajorAxisCm * FMath::Cos(Angle))
			+ DebugData.TargetRight * (DebugData.Settings.ImpactEllipseSemiMinorAxisCm * FMath::Sin(Angle));
		DrawDebugLine(World, PreviousEllipsePoint, EllipsePoint,
			FMath::Sin(Angle) >= 0.0f ? FColor::Yellow : FColor::Cyan,
			false, DebugDuration, 0, 5.0f);
		PreviousEllipsePoint = EllipsePoint;
	}
	DrawDebugSphere(World, DebugData.EllipseCenter, 55.0f, 12, FColor::White, false, DebugDuration, 0, 4.0f);
	DrawDebugSphere(World, DebugData.RequestedImpactPoint, 70.0f, 12, FColor::Orange, false, DebugDuration, 0, 5.0f);
	DrawDebugSphere(World, DebugData.ClampedImpactPoint, 90.0f, 16, FColor::Green, false, DebugDuration, 0, 7.0f);
	DrawDebugDirectionalArrow(World, DebugData.Start, DebugData.ClampedImpactPoint, 180.0f,
		FColor::Green, false, DebugDuration, 0, 4.0f);
}

FEnemyShipCannonVolleySettings UGA_EnemyShipCannonVolley::ResolveCannonVolleySettings(const AEnemyShip& Ship)
{
	FEnemyShipCannonVolleySettings Settings;
	if (Ship.EnemyShipArchetype)
	{
		for (const UEnemyShipSkillModuleData* Module : Ship.EnemyShipArchetype->SkillModules)
		{
			if (Module && Module->GetAbilityTag() == GameplayAbility_EnemyShip_CannonVolley)
			{
				Settings = Module->CannonVolleySettings;
				break;
			}
		}
	}
	Settings.MinimumElevationDegrees = FMath::Clamp(Settings.MinimumElevationDegrees, 0.0f, 45.0f);
	Settings.ImpactEllipseSemiMajorAxisCm = FMath::Max(1.0f, Settings.ImpactEllipseSemiMajorAxisCm);
	Settings.ImpactEllipseSemiMinorAxisCm = FMath::Max(1.0f, Settings.ImpactEllipseSemiMinorAxisCm);
	Settings.AttackerFacingHalfWeight = FMath::Max(0.0f, Settings.AttackerFacingHalfWeight);
	Settings.AttackerOppositeHalfWeight = FMath::Max(0.0f, Settings.AttackerOppositeHalfWeight);
	if (Settings.AttackerFacingHalfWeight + Settings.AttackerOppositeHalfWeight <= KINDA_SMALL_NUMBER)
	{
		Settings.AttackerFacingHalfWeight = 1.0f;
		Settings.AttackerOppositeHalfWeight = 1.0f;
	}
	return Settings;
}

bool UGA_EnemyShipCannonVolley::CalculateRangeAtElevation(float ProjectileSpeed, float GravityMagnitude,
	float DeltaZ, float ElevationRadians, float& OutRangeCm, float& OutFlightTime)
{
	OutRangeCm = 0.0f;
	OutFlightTime = 0.0f;
	if (ProjectileSpeed <= KINDA_SMALL_NUMBER || GravityMagnitude <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const float VerticalSpeed = ProjectileSpeed * FMath::Sin(ElevationRadians);
	const float Discriminant = FMath::Square(VerticalSpeed) - 2.0f * GravityMagnitude * DeltaZ;
	if (Discriminant < 0.0f)
	{
		return false;
	}
	OutFlightTime = (VerticalSpeed + FMath::Sqrt(Discriminant)) / GravityMagnitude;
	OutRangeCm = ProjectileSpeed * FMath::Cos(ElevationRadians) * OutFlightTime;
	return OutFlightTime >= 0.0f && OutRangeCm >= 0.0f;
}

bool UGA_EnemyShipCannonVolley::CalculateLowArc(float ProjectileSpeed, float GravityMagnitude,
	float HorizontalRangeCm, float DeltaZ, float& OutElevationRadians, float& OutFlightTime)
{
	OutElevationRadians = 0.0f;
	OutFlightTime = 0.0f;
	if (ProjectileSpeed <= KINDA_SMALL_NUMBER || GravityMagnitude <= KINDA_SMALL_NUMBER
		|| HorizontalRangeCm <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const double SpeedSquared = FMath::Square(static_cast<double>(ProjectileSpeed));
	double Discriminant = FMath::Square(SpeedSquared) - static_cast<double>(GravityMagnitude)
		* (static_cast<double>(GravityMagnitude) * FMath::Square(static_cast<double>(HorizontalRangeCm))
			+ 2.0 * static_cast<double>(DeltaZ) * SpeedSquared);
	const double ErrorTolerance = FMath::Max(1.0, FMath::Square(SpeedSquared) * 1.e-6);
	if (Discriminant < -ErrorTolerance)
	{
		return false;
	}
	Discriminant = FMath::Max(0.0, Discriminant);
	const double TanElevation = (SpeedSquared - FMath::Sqrt(Discriminant))
		/ (static_cast<double>(GravityMagnitude) * HorizontalRangeCm);
	OutElevationRadians = static_cast<float>(FMath::Atan(TanElevation));
	const float HorizontalSpeed = ProjectileSpeed * FMath::Cos(OutElevationRadians);
	if (HorizontalSpeed <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	OutFlightTime = HorizontalRangeCm / HorizontalSpeed;
	return true;
}

FVector UGA_EnemyShipCannonVolley::SampleImpactEllipseOffset(FRandomStream& RandomStream,
	const FVector& TargetForward2D, const FVector& TargetRight2D,
	const FVector& DirectionFromTargetToAttacker2D, const FEnemyShipCannonVolleySettings& Settings,
	bool& bOutFacingHalf)
{
	const float FacingWeight = FMath::Max(0.0f, Settings.AttackerFacingHalfWeight);
	const float OppositeWeight = FMath::Max(0.0f, Settings.AttackerOppositeHalfWeight);
	const float WeightSum = FacingWeight + OppositeWeight;
	const float FacingProbability = WeightSum > KINDA_SMALL_NUMBER ? FacingWeight / WeightSum : 0.5f;
	bOutFacingHalf = RandomStream.FRand() < FacingProbability;
	const float Radius = FMath::Sqrt(RandomStream.FRand());
	const float Angle = 2.0f * PI * RandomStream.FRand();
	const float Longitudinal = FMath::Max(1.0f, Settings.ImpactEllipseSemiMajorAxisCm)
		* Radius * FMath::Cos(Angle);
	const float UnsignedLateral = FMath::Abs(FMath::Max(1.0f, Settings.ImpactEllipseSemiMinorAxisCm)
		* Radius * FMath::Sin(Angle));
	const float AttackerSide = FVector::DotProduct(DirectionFromTargetToAttacker2D, TargetRight2D) < -KINDA_SMALL_NUMBER
		? -1.0f : 1.0f;
	const float Lateral = UnsignedLateral * AttackerSide * (bOutFacingHalf ? 1.0f : -1.0f);
	return TargetForward2D * Longitudinal + TargetRight2D * Lateral;
}

FVector UGA_EnemyShipCannonVolley::ClampImpactPointToRange(const FVector& ShotStart,
	const FVector& RequestedImpactPoint, float MaximumRangeCm, bool& bOutWasClamped)
{
	FVector HorizontalOffset = RequestedImpactPoint - ShotStart;
	HorizontalOffset.Z = 0.0f;
	const float SafeMaximumRange = FMath::Max(0.0f, MaximumRangeCm);
	bOutWasClamped = HorizontalOffset.SizeSquared() > FMath::Square(SafeMaximumRange);
	if (bOutWasClamped)
	{
		HorizontalOffset = HorizontalOffset.GetSafeNormal() * SafeMaximumRange;
	}
	FVector Result = ShotStart + HorizontalOffset;
	Result.Z = RequestedImpactPoint.Z;
	return Result;
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
