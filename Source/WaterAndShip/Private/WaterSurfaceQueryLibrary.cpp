#include "WaterSurfaceQueryLibrary.h"

#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "WaterBodyTypes.h"
#include "WaterWaves.h"

bool FWaterSurfaceQueryLibrary::QueryWaterSurface(
	const UWorld* World,
	const FVector& Location,
	float& OutWaterSurfaceZ,
	bool bIncludeWaveHeight)
{
	if (!World)
	{
		return false;
	}

	const AGameStateBase* GameState = World->GetGameState();
	const double QueryTime = GameState
		? GameState->GetServerWorldTimeSeconds()
		: World->GetTimeSeconds();

	bool bFoundWater = false;
	float HighestWaterZ = -TNumericLimits<float>::Max();
	for (TActorIterator<AWaterBody> It(World); It; ++It)
	{
		float CandidateZ = 0.0f;
		if (QueryWaterBodySurface(*It, Location, QueryTime, CandidateZ, bIncludeWaveHeight)
			&& (!bFoundWater || CandidateZ > HighestWaterZ))
		{
			bFoundWater = true;
			HighestWaterZ = CandidateZ;
		}
	}

	if (bFoundWater)
	{
		OutWaterSurfaceZ = HighestWaterZ;
	}
	return bFoundWater;
}

bool FWaterSurfaceQueryLibrary::QueryWaterBodySurface(
	const AWaterBody* WaterBody,
	const FVector& Location,
	double QueryTimeSeconds,
	float& OutWaterSurfaceZ,
	bool bIncludeWaveHeight)
{
	if (!WaterBody)
	{
		return false;
	}

	UWaterBodyComponent* WaterBodyComponent = WaterBody->GetWaterBodyComponent();
	if (!WaterBodyComponent)
	{
		return false;
	}

	const EWaterBodyQueryFlags QueryFlags =
		EWaterBodyQueryFlags::ComputeLocation | EWaterBodyQueryFlags::ComputeDepth;
	float SplineInputKey = -1.0f;
	if (WaterBodyComponent->GetWaterBodyType() == EWaterBodyType::River)
	{
		SplineInputKey = WaterBodyComponent->FindInputKeyClosestToWorldLocation(Location);
	}

	const TValueOrError<FWaterBodyQueryResult, EWaterBodyQueryError> QueryResult =
		WaterBodyComponent->TryQueryWaterInfoClosestToWorldLocation(Location, QueryFlags, SplineInputKey);
	if (!QueryResult.HasValue())
	{
		return false;
	}

	const FWaterBodyQueryResult& WaterInfo = QueryResult.GetValue();
	float WaterZ = WaterInfo.GetWaterSurfaceLocation().Z;
	if (bIncludeWaveHeight && WaterBodyComponent->HasWaves())
	{
		if (UWaterWavesBase* WaterWaves = WaterBodyComponent->GetWaterWaves())
		{
			const float WaterDepth = WaterInfo.GetWaterSurfaceDepth();
			const float Attenuation = WaterWaves->GetWaveAttenuationFactor(
				WaterInfo.GetWaterSurfaceLocation(), WaterDepth, WaterBodyComponent->TargetWaveMaskDepth);
			if (Attenuation > 0.0f)
			{
				FVector SurfaceNormal = FVector::UpVector;
				WaterZ += WaterWaves->GetWaveHeightAtPosition(
					WaterInfo.GetWaterSurfaceLocation(),
					WaterDepth,
					static_cast<float>(QueryTimeSeconds),
					SurfaceNormal) * Attenuation;
			}
		}
	}

	OutWaterSurfaceZ = WaterZ;
	return true;
}
