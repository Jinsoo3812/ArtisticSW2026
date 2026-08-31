#include "ShipAI/EnemyShipNavigationTypes.h"

namespace
{
	FVector HorizontalDirection(const FVector& From, const FVector& To, float& OutDistance)
	{
		FVector Delta = To - From;
		Delta.Z = 0.0f;
		OutDistance = Delta.Size();
		return Delta.GetSafeNormal();
	}

	FVector SafeHorizontal(const FVector& Vector, const FVector& Fallback)
	{
		FVector Result = Vector;
		Result.Z = 0.0f;
		return Result.Normalize() ? Result : Fallback;
	}
}

FEnemyShipNavigationOutput FEnemyShipNavigationModel::Evaluate(
	ENavalCombatState CurrentState,
	const FEnemyShipNavigationProfile& Profile,
	const FEnemyShipNavigationContext& Context)
{
	FEnemyShipNavigationOutput Output;
	Output.State = CurrentState;

	float TargetDistance = 0.0f;
	const FVector ToTarget = Context.bHasTarget
		? HorizontalDirection(Context.ShipLocation, Context.TargetLocation, TargetDistance)
		: FVector::ZeroVector;
	float HomeDistance = 0.0f;
	const FVector ToHome = Context.bHasHome
		? HorizontalDirection(Context.ShipLocation, Context.HomeLocation, HomeDistance)
		: FVector::ZeroVector;
	Output.TargetDistance = TargetDistance;
	Output.HomeDistance = HomeDistance;

	const bool bTargetInDetectionRange = Context.bHasTarget && TargetDistance <= FMath::Max(0.0f, Profile.DetectionDistance);
	const float IdealDistance = FMath::Max(1.0f, Profile.IdealDistance);
	const float DangerDistance = FMath::Clamp(Profile.DangerCloseDistance, 0.0f, IdealDistance);
	const float ReturnTriggerDistance = FMath::Max(Profile.ReturnArrivalDistance, Profile.ReturnTriggerDistance);

	if (Output.State == ENavalCombatState::Return)
	{
		// Return is latched. Detection and combat-range rules remain suspended until
		// the ship reaches its Return Point (or the Return Point becomes invalid).
		if (!Context.bHasHome || HomeDistance <= Profile.ReturnArrivalDistance)
		{
			Output.State = ENavalCombatState::Idle;
		}
	}
	else if (Context.bHasHome
		&& (HomeDistance > ReturnTriggerDistance
			|| (Context.bReturnRequested && HomeDistance > Profile.ReturnArrivalDistance)))
	{
		// Home leash has priority over every combat/detection range rule.
		Output.State = ENavalCombatState::Return;
	}
	else if (Output.State == ENavalCombatState::Idle)
	{
		if (bTargetInDetectionRange)
		{
			Output.State = ENavalCombatState::Approach;
		}
	}
	else if (!bTargetInDetectionRange)
	{
		Output.State = ENavalCombatState::Idle;
	}
	else if (Output.State == ENavalCombatState::Approach)
	{
		if (TargetDistance <= DangerDistance)
		{
			Output.State = ENavalCombatState::Retreat;
		}
		else if (TargetDistance <= IdealDistance)
		{
			Output.State = ENavalCombatState::Orbit;
		}
	}
	else if (Output.State == ENavalCombatState::Retreat && TargetDistance >= IdealDistance)
	{
		Output.State = ENavalCombatState::Orbit;
	}
	else if (Output.State == ENavalCombatState::Orbit)
	{
		if (TargetDistance > IdealDistance + FMath::Max(0.0f, Profile.OrbitTolerance))
		{
			Output.State = ENavalCombatState::Approach;
		}
		else if (TargetDistance <= DangerDistance)
		{
			Output.State = ENavalCombatState::Retreat;
		}
	}

	float SpeedFactor = 1.0f;
	switch (Output.State)
	{
	case ENavalCombatState::Idle:
		Output.DesiredHeading = SafeHorizontal(Context.ShipForward, FVector::ForwardVector);
		SpeedFactor = 0.0f;
		break;
	case ENavalCombatState::Return:
		Output.DesiredHeading = ToHome;
		break;
	case ENavalCombatState::Approach:
		Output.DesiredHeading = ToTarget;
		break;
	case ENavalCombatState::Orbit:
		{
			const FVector Tangent = Profile.bOrbitClockwise
				? FVector(-ToTarget.Y, ToTarget.X, 0.0f)
				: FVector(ToTarget.Y, -ToTarget.X, 0.0f);
			const float SteeringBias = FMath::Clamp((TargetDistance - IdealDistance) / IdealDistance, -0.4f, 0.4f);
			Output.DesiredHeading = (Tangent + ToTarget * SteeringBias).GetSafeNormal();
		}
		break;
	case ENavalCombatState::Retreat:
		Output.DesiredHeading = -ToTarget;
		break;
	}

	const FVector ShipForward = SafeHorizontal(Context.ShipForward, FVector::ForwardVector);
	const FVector ShipRight = SafeHorizontal(Context.ShipRight, FVector::RightVector);
	const float HeadingDot = FVector::DotProduct(ShipForward, Output.DesiredHeading);
	const float RightDot = FVector::DotProduct(ShipRight, Output.DesiredHeading);

	if (HeadingDot < 0.99f && SpeedFactor > KINDA_SMALL_NUMBER)
	{
		Output.TurnInput = RightDot > 0.0f ? 1.0f : -1.0f;
	}
	if (HeadingDot > 0.0f && SpeedFactor > KINDA_SMALL_NUMBER)
	{
		Output.MoveInput = HeadingDot * SpeedFactor;
	}

	Output.MoveInput = FMath::Clamp(Output.MoveInput * FMath::Max(0.0f, Profile.ForwardInputScale), -1.0f, 1.0f);
	Output.TurnInput = FMath::Clamp(Output.TurnInput * FMath::Max(0.0f, Profile.TurnInputScale), -1.0f, 1.0f);
	return Output;
}
