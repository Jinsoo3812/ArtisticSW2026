#include "ShipAI/EnemyShipWeakeningData.h"

bool UEnemyShipWeakeningData::Evaluate(float HealthRatio, FEnemyShipWeakeningPoint& OutPoint) const
{
	OutPoint = FEnemyShipWeakeningPoint();
	if (Points.Num() < 2 || !FMath::IsNearlyZero(Points[0].ShipHealthRatio)
		|| !FMath::IsNearlyEqual(Points.Last().ShipHealthRatio, 1.f))
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid weakening curve endpoints in %s"), *GetNameSafe(this));
		return false;
	}
	float PreviousRatio = -1.f;
	for (const FEnemyShipWeakeningPoint& Point : Points)
	{
		if (!FMath::IsFinite(Point.ShipHealthRatio) || Point.ShipHealthRatio <= PreviousRatio
			|| Point.ShipHealthRatio < 0.f || Point.ShipHealthRatio > 1.f
			|| !FMath::IsFinite(Point.StrengthMultiplier)
			|| !FMath::IsFinite(Point.MoveSpeedMultiplier)
			|| !FMath::IsFinite(Point.AttackSpeedMultiplier)
			|| Point.StrengthMultiplier < 0.f || Point.StrengthMultiplier > 1.f
			|| Point.MoveSpeedMultiplier <= 0.f || Point.MoveSpeedMultiplier > 1.f
			|| Point.AttackSpeedMultiplier <= 0.f || Point.AttackSpeedMultiplier > 1.f)
		{
			UE_LOG(LogTemp, Error, TEXT("Invalid weakening curve point in %s"), *GetNameSafe(this));
			return false;
		}
		PreviousRatio = Point.ShipHealthRatio;
	}
	const float Clamped = FMath::Clamp(HealthRatio, 0.f, 1.f);
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		const FEnemyShipWeakeningPoint& High = Points[Index];
		if (Clamped > High.ShipHealthRatio) continue;
		const FEnemyShipWeakeningPoint& Low = Points[Index - 1];
		const float Alpha = (Clamped - Low.ShipHealthRatio) / (High.ShipHealthRatio - Low.ShipHealthRatio);
		OutPoint.ShipHealthRatio = Clamped;
		OutPoint.StrengthMultiplier = FMath::Lerp(Low.StrengthMultiplier, High.StrengthMultiplier, Alpha);
		OutPoint.MoveSpeedMultiplier = FMath::Lerp(Low.MoveSpeedMultiplier, High.MoveSpeedMultiplier, Alpha);
		OutPoint.AttackSpeedMultiplier = FMath::Lerp(Low.AttackSpeedMultiplier, High.AttackSpeedMultiplier, Alpha);
		return true;
	}
	return true;
}
