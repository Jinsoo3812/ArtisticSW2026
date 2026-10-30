#include "Animation/CannonOperatorHeadAim.h"

FQuat SWCannonHeadAim::Target(const FQuat& Body, const FVector& Bore, const FVector& ToGrip,
	float GripWeight, const FVector& Forward, const FVector& Right, const FVector& Up,
	const FSWCannonHeadLimits& Limits)
{
	const FVector BoreDirection = Bore.GetSafeNormal(UE_SMALL_NUMBER, Forward);
	FVector Direction = FMath::Lerp(BoreDirection,
		ToGrip.GetSafeNormal(UE_SMALL_NUMBER, BoreDirection), FMath::Clamp(GripWeight, 0.f, 1.f));
	Direction = Body.UnrotateVector(Direction.GetSafeNormal(UE_SMALL_NUMBER, BoreDirection));
	// Rearward/vertical targets must not select opposite +/-180 degree yaw branches.
	// Project into the chest's forward hemisphere before imposing anatomical limits.
	const float F = FMath::Max(.05f, FVector::DotProduct(Direction, Forward));
	const float R = FVector::DotProduct(Direction, Right);
	const float U = FVector::DotProduct(Direction, Up);
	const float Yaw = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(R, F)),
		-FMath::Max(0.f, Limits.Yaw), FMath::Max(0.f, Limits.Yaw));
	const float Pitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(-U, FMath::Sqrt(F*F + R*R))),
		-FMath::Max(0.f, Limits.PitchUp), FMath::Max(0.f, Limits.PitchDown));
	FQuat Correction = (FQuat(Up, FMath::DegreesToRadians(Yaw))
		* FQuat(Right, FMath::DegreesToRadians(Pitch))).GetNormalized();
	const float Angle = FMath::RadiansToDegrees(FQuat::Identity.AngularDistance(Correction));
	if (Angle > FMath::Max(0.f, Limits.Cone))
		Correction = FQuat::Slerp(FQuat::Identity, Correction, FMath::Max(0.f, Limits.Cone) / Angle).GetNormalized();
	return Correction;
}

FQuat SWCannonHeadAim::Follow(const FQuat& Current, const FQuat& Desired, float DeltaTime,
	const FVector& Forward, const FVector& Right, const FVector& Up, const FSWCannonHeadLimits& Limits)
{
	if (DeltaTime <= 0.f) return Current;
	const auto Angles = [&](const FQuat& Q)
	{
		const FVector D=Q.RotateVector(Forward);
		const float F=FVector::DotProduct(D,Forward), R=FVector::DotProduct(D,Right), U=FVector::DotProduct(D,Up);
		return FVector2D(FMath::RadiansToDegrees(FMath::Atan2(R,F)),
			FMath::RadiansToDegrees(FMath::Atan2(-U,FMath::Sqrt(F*F+R*R))));
	};
	const FVector2D Start=Angles(Current), End=Angles(Desired), Delta=End-Start;
	const float Distance=Delta.Size();
	if (Distance < UE_SMALL_NUMBER) return Desired;
	const float Ease = 1.f - FMath::Exp(-FMath::Max(0.f, Limits.FollowSpeed) * DeltaTime);
	const float Rate = FMath::Max(0.f, Limits.DegreesPerSecond) * DeltaTime / Distance;
	// Interpolate bounded anatomical angles. Slerping two pitch-limit rotations
	// can bow above that limit at their midpoint even though both ends are valid.
	const FVector2D Next=Start+Delta*FMath::Min(Ease,Rate);
	return (FQuat(Up,FMath::DegreesToRadians(Next.X))*FQuat(Right,FMath::DegreesToRadians(Next.Y))).GetNormalized();
}
