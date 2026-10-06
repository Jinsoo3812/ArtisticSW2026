#include "RangedEnemy/EnemyBowShotPreparation.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Item/Projectiles/ProjectileShotPreparation.h"
#include "Movement/MovementFrameVelocity.h"
#include "Ship.h"

namespace
{
	TAutoConsoleVariable<float> CVarEnemyBowGravityScale(TEXT("sw.EnemyBow.GravityScale"), -1.0f,
		TEXT("Enemy bow new shots only: -1 uses BP authored gravity; 0 disables gravity for aim diagnosis."), ECVF_Cheat);
	TAutoConsoleVariable<int32> CVarEnemyBowSameShipCompensation(TEXT("sw.EnemyBow.SameShipCompensation"), 1,
		TEXT("Compensate same-ship rigid motion and gravity for new Enemy arrows. Never predicts player walking. 0=direct aim, 1=enabled."), ECVF_Cheat);

	const AShip* GetCurrentShip(const AActor* Actor)
	{
		if (const ACharacter* Character = Cast<ACharacter>(Actor))
		{
			const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
			if (!Movement || Movement->IsFalling()) return nullptr;
		}
		FName Bone;
		// Resolve the actual support/attachment chain, never a stale HostShip or Owner.
		for (const USceneComponent* Component = MovementFrameVelocity::GetCarrier(Actor, Bone);
			Component; Component = Component->GetAttachParent())
		{
			if (const AShip* Ship = Cast<AShip>(Component->GetOwner())) return Ship;
		}
		return nullptr;
	}

	struct FShipMotion
	{
		FVector Pivot;
		FVector LinearVelocity;
		FVector AngularVelocity;

		FVector PredictPoint(const FVector& Point, double Time) const
		{
			const double AngularSpeed = AngularVelocity.Size();
			const FQuat Rotation = AngularSpeed > UE_SMALL_NUMBER
				? FQuat(AngularVelocity / AngularSpeed, AngularSpeed * Time) : FQuat::Identity;
			return Pivot + LinearVelocity * Time + Rotation.RotateVector(Point - Pivot);
		}
	};

	bool SolveSameShipShot(const FShipMotion& Motion, const FProjectileShotInput& Input,
		FVector& OutVelocity, FVector& OutIntercept, double& OutTime)
	{
		const FVector Muzzle = Input.MuzzleTransform.GetLocation();
		const FVector Gravity(0.0, 0.0, Input.GravityZ);
		auto RequiredDisplacement = [&](double Time)
		{
			return Motion.PredictPoint(Input.AimPoint, Time) - Muzzle - 0.5 * Gravity * Time * Time;
		};
		// Find the first reachable intercept (the fast arc), bounded by the normal arrow lifetime.
		// Player locomotion is deliberately absent: only the captured rigid ship motion is extrapolated.
		constexpr double Step = 1.0 / 120.0;
		constexpr int32 MaxSteps = 1200;
		double Lower = 0.0;
		for (int32 Index = 1; Index <= MaxSteps; ++Index)
		{
			double Upper = Index * Step;
			const double Error = RequiredDisplacement(Upper).Size() - Input.Speed * Upper;
			if (!FMath::IsFinite(Error)) return false;
			if (Error <= 0.0)
			{
				for (int32 Iteration = 0; Iteration < 40; ++Iteration)
				{
					const double Middle = 0.5 * (Lower + Upper);
					if (RequiredDisplacement(Middle).Size() > Input.Speed * Middle) Lower = Middle;
					else Upper = Middle;
				}
				OutTime = 0.5 * (Lower + Upper);
				OutVelocity = RequiredDisplacement(OutTime) / OutTime;
				OutIntercept = Motion.PredictPoint(Input.AimPoint, OutTime);
				return !OutVelocity.ContainsNaN() && !OutIntercept.ContainsNaN();
			}
			Lower = Upper;
		}
		return false;
	}
}

double EnemyBowShotPreparation::GetGravityScale(double AuthoredScale)
{
	const float Override = CVarEnemyBowGravityScale.GetValueOnGameThread();
	return FMath::IsFinite(Override) && Override >= 0.0f ? Override : AuthoredScale;
}

bool EnemyBowShotPreparation::Prepare(const AActor* Shooter, const AActor* Target, const FProjectileShotInput& Input,
	FProjectileShotSnapshot& OutShot)
{
	OutShot = FProjectileShotSnapshot();
	if (!IsValid(Shooter) || !Shooter->GetWorld() || !Input.ShotId.IsValid()
		|| Input.MuzzleTransform.ContainsNaN() || Input.AimPoint.ContainsNaN()
		|| !FMath::IsFinite(Input.Speed) || Input.Speed <= 0.0
		|| !FMath::IsFinite(Input.GravityZ) || !FMath::IsFinite(Input.AimServerTime)) return false;
	const FVector Direction = (Input.AimPoint - Input.MuzzleTransform.GetLocation()).GetSafeNormal();
	if (Direction.IsNearlyZero()) return false;
	OutShot.Input = Input;
	OutShot.Input.Profile.VelocityPolicy = EProjectileVelocityPolicy::WorldAim;
	OutShot.WorldVelocity = Direction * Input.Speed;
	const AShip* ShooterShip = GetCurrentShip(Shooter);
	const AShip* TargetShip = IsValid(Target) ? GetCurrentShip(Target) : nullptr;
	const bool bSameShip = ShooterShip && ShooterShip == TargetShip;
	bool bCompensated = false;
	double FlightTime = 0.0;
	FVector Intercept = Input.AimPoint;
	FShipMotion Motion{};
	const TCHAR* Reason = TEXT("DifferentSupport");
	if (bSameShip && CVarEnemyBowSameShipCompensation.GetValueOnGameThread() != 0)
	{
		UStaticMeshComponent* Body = ShooterShip->BuoyancyRoot;
		Reason = TEXT("UnavailableShipMotion");
		if (IsValid(Body) && Body->IsSimulatingPhysics())
		{
			Motion.Pivot = Body->GetCenterOfMass();
			Motion.LinearVelocity = Body->GetPhysicsLinearVelocity();
			Motion.AngularVelocity = Body->GetPhysicsAngularVelocityInRadians();
			if (!Motion.Pivot.ContainsNaN() && !Motion.LinearVelocity.ContainsNaN() && !Motion.AngularVelocity.ContainsNaN())
			{
				FVector Velocity;
				bCompensated = SolveSameShipShot(Motion, Input, Velocity, Intercept, FlightTime);
				if (bCompensated) OutShot.WorldVelocity = Velocity;
				Reason = bCompensated ? TEXT("SameShip") : TEXT("NoReachableIntercept");
			}
		}
	}
	else if (bSameShip) Reason = TEXT("Disabled");
	// Keep AimPoint as the current target for combat LOS. AimDirection records the actual launch direction.
	OutShot.Input.AimDirection = OutShot.WorldVelocity.GetSafeNormal();
	OutShot.SpawnTransform = FTransform(OutShot.WorldVelocity.Rotation(), Input.MuzzleTransform.GetLocation());
	OutShot.CommitServerTime = ProjectileShotPreparation::GetServerTime(Shooter->GetWorld());
	OutShot.CommitFrame = GFrameCounter;
	// Motion is diagnostic metadata only; it never changes Enemy bow initial velocity.
	OutShot.bHasMotionSample = MovementFrameVelocity::TryGetActorPointVelocity(Shooter,
		Input.MuzzleTransform.GetLocation(), OutShot.ShooterVelocity);
	if (!OutShot.bHasMotionSample) OutShot.ShooterVelocity = FVector::ZeroVector;
	FName Bone;
	if (const USceneComponent* Carrier = MovementFrameVelocity::GetCarrier(Shooter, Bone))
		OutShot.CarrierName = Carrier->GetFName();
	const IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("sw.Projectile.DebugLaunch"));
	if (Debug && Debug->GetInt() != 0)
	{
		UE_LOG(LogTemp, Display, TEXT("[EnemyBowCompensation] Id=%s Shooter=%s Target=%s ShooterShip=%s TargetShip=%s Applied=%d Reason=%s FlightTime=%.4f Current=%s Intercept=%s ShipV=%s ShipW=%s"),
			*Input.ShotId.ToString(), *GetNameSafe(Shooter), *GetNameSafe(Target), *GetNameSafe(ShooterShip),
			*GetNameSafe(TargetShip), bCompensated, Reason, FlightTime, *Input.AimPoint.ToString(),
			*Intercept.ToString(), *Motion.LinearVelocity.ToString(), *Motion.AngularVelocity.ToString());
		if (bCompensated)
		{
			DrawDebugPoint(Shooter->GetWorld(), Intercept, 16.0f, FColor::Blue, false, 3.0f);
			DrawDebugLine(Shooter->GetWorld(), Input.AimPoint, Intercept, FColor::Blue, false, 3.0f);
		}
	}
	return true;
}
