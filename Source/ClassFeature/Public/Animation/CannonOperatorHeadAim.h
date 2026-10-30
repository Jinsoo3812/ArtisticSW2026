#pragma once

#include "CoreMinimal.h"

/** Anatomical angles in the operator's reference forward/right/up frame. */
struct FSWCannonHeadLimits
{
	float Yaw = 45.f;
	float PitchUp = 35.f;
	float PitchDown = 50.f;
	float Cone = 80.f;
	float FollowSpeed = 8.f;
	float DegreesPerSecond = 120.f;
};

namespace SWCannonHeadAim
{
	/** Blend directions before creating rotations: a rear/down contact has no stable yaw. */
	CLASSFEATURE_API FQuat Target(const FQuat& Body, const FVector& Bore, const FVector& ToGrip,
		float GripWeight, const FVector& Forward, const FVector& Right, const FVector& Up,
		const FSWCannonHeadLimits& Limits);
	/** Body-local correction; the neck follows the chest without accumulating world rotation. */
	CLASSFEATURE_API FQuat Follow(const FQuat& Current, const FQuat& Target, float DeltaTime,
		const FVector& Forward, const FVector& Right, const FVector& Up, const FSWCannonHeadLimits& Limits);
}
