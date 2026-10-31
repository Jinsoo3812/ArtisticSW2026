#pragma once

#include "CoreMinimal.h"
#include "ShipAI/EnemyShipNavigationTypes.h"

class AEnemyShip;
class UEnemyShipAvoidanceSettings;
class UWorld;

namespace EnemyShipLocalSafety
{
	float Value(float Input, float Fallback, float Minimum = 0.0f, float Maximum = 10000.0f);

	struct FHull
	{
		TWeakObjectPtr<AActor> Actor;
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FVector CenterOffset = FVector::ZeroVector;
		FVector Extent = FVector::ZeroVector;
		float Yaw = 0.0f;
		float YawRate = 0.0f;
		float Acceleration = 0.0f;
		float YawAcceleration = 0.0f;
		float Damping = 0.0f;
		float AngularDamping = 0.0f;
		float MaximumSpeed = 0.0f;
		float MaximumYawRate = 0.0f;
		bool bHold = false;
		FVector Center() const;
		void Advance(float Dt);
	};

	enum class EThreat : uint8 { None, Terrain, Ship, Invalid };
	struct FResult
	{
		bool bSafe = true;
		EThreat Threat = EThreat::None;
		TWeakObjectPtr<AActor> Actor;
		const TCHAR* Reason = TEXT("Clear");
		float InitialGap = -1.0f;
		float PreviousGap = -1.0f;
		float Gap = -1.0f;
		float Envelope = 0.0f;
		float Margin = 0.0f;
		float PredictionTime = 0.0f;
		float HitDistance = -1.0f;
		float PenetrationDepth = 0.0f;
		bool bStartPenetrating = false;
		int32 Segment = 0;
		int32 Segments = 0;
		int32 Peers = 0;
		int32 TestedPeers = 0;
		int32 TerrainSweeps = 0;
		FName ComponentName;
		float ForecastAcceleration = 0.0f;
		float ForecastYawRate = 0.0f;
		float ForecastYawAcceleration = 0.0f;
		float LinearDamping = 0.0f;
		float AngularDamping = 0.0f;
		FVector HullExtent = FVector::ZeroVector;
	};

	struct FQuery
	{
		FQuery(AEnemyShip& Ship, const TArray<TWeakObjectPtr<AActor>>& WaterBodies);
		FResult Check(const FEnemyShipNavigationOverrideRequest& Control, bool bEscape, float ExtraMargin = 0.0f) const;
		FResult CheckTransform(const FTransform& Start, const FTransform& End) const;
		bool IsValid() const { return bValid; }

	private:
		FResult CheckPath(const TArray<FHull>& Path, float Duration, float Margin) const;
		AEnemyShip& Owner;
		const UEnemyShipAvoidanceSettings& Settings;
		FHull Hull;
		TArray<FHull> Peers;
		TArray<TWeakObjectPtr<AActor>> IgnoredWater;
		bool bValid = false;
	};

	bool HasTerrainAhead(AEnemyShip& Ship, float Distance);
}
