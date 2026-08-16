#include "SWRippleWaterWaves.h"
#include "RippleSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "SWShipWakeSubsystem.h"
#include "Water/SWRippleStateSubsystem.h"

USWRippleWaterWaves::USWRippleWaterWaves()
{
}

float USWRippleWaterWaves::GetMaxWaveHeight() const
{
	float MaxHeight = 0.0f;
	if (BaseWavesAsset)
	{
		if (const UWaterWaves* ActualWaves = BaseWavesAsset->GetWaterWaves())
		{
			MaxHeight = ActualWaves->GetMaxWaveHeight();
		}
	}
	
	// Advertise the shared signed allowance used by ripple and ship-wake overlays.
	return MaxHeight + 100.0f;
}

float USWRippleWaterWaves::GetWaveHeightAtPosition(const FVector& InPosition, float InWaterDepth, float InTime, FVector& OutNormal) const
{
	float SyncTime = InTime;
	UWorld* World = GetWorld();
	if (World)
	{
		// 게임 스레드 틱(예: 수영, 발사체 등)에서 호출될 때만 서버 동기화 시간으로 보정합니다.
		// 비동기 물리 틱(Async Physics)에서 호출될 때의 InTime은 서버-클라가 동일하게 축적하는 고정 SimTime이므로 그대로 사용합니다.
		float TimeSeconds = World->GetTimeSeconds();
		if (FMath::IsNearlyEqual(InTime, TimeSeconds, 0.001f))
		{
			if (AGameStateBase* GameState = World->GetGameState())
			{
				SyncTime = GameState->GetServerWorldTimeSeconds();
			}
		}
	}

	float Height = 0.0f;
	if (BaseWavesAsset)
	{
		if (const UWaterWaves* ActualWaves = BaseWavesAsset->GetWaterWaves())
		{
			Height = ActualWaves->GetWaveHeightAtPosition(InPosition, InWaterDepth, SyncTime, OutNormal);
		}
		else
		{
			OutNormal = FVector::UpVector;
		}
	}
	else
	{
		OutNormal = FVector::UpVector;
	}

	if (World)
	{
		if (USWRippleStateSubsystem* Subsystem = World->GetSubsystem<USWRippleStateSubsystem>())
		{
			Height += Subsystem->GetRippleHeight(InPosition, static_cast<double>(SyncTime));
		}

		if (USWShipWakeSubsystem* WakeSubsystem = World->GetSubsystem<USWShipWakeSubsystem>())
		{
			Height += WakeSubsystem->GetWakeHeight(InPosition, static_cast<double>(SyncTime));
			const FVector2D WakeGradient = WakeSubsystem->GetWakeGradient(InPosition, static_cast<double>(SyncTime));
			OutNormal = FVector(
				OutNormal.X - WakeGradient.X,
				OutNormal.Y - WakeGradient.Y,
				OutNormal.Z).GetSafeNormal();
		}
	}

	return Height;
}

float USWRippleWaterWaves::GetSimpleWaveHeightAtPosition(const FVector& InPosition, float InWaterDepth, float InTime) const
{
	float SyncTime = InTime;
	UWorld* World = GetWorld();
	if (World)
	{
		float TimeSeconds = World->GetTimeSeconds();
		if (FMath::IsNearlyEqual(InTime, TimeSeconds, 0.001f))
		{
			if (AGameStateBase* GameState = World->GetGameState())
			{
				SyncTime = GameState->GetServerWorldTimeSeconds();
			}
		}
	}

	float Height = 0.0f;
	if (BaseWavesAsset)
	{
		if (const UWaterWaves* ActualWaves = BaseWavesAsset->GetWaterWaves())
		{
			Height = ActualWaves->GetSimpleWaveHeightAtPosition(InPosition, InWaterDepth, SyncTime);
		}
	}

	if (World)
	{
		if (USWRippleStateSubsystem* Subsystem = World->GetSubsystem<USWRippleStateSubsystem>())
		{
			Height += Subsystem->GetRippleHeight(InPosition, static_cast<double>(SyncTime));
		}
		if (USWShipWakeSubsystem* WakeSubsystem = World->GetSubsystem<USWShipWakeSubsystem>())
		{
			Height += WakeSubsystem->GetWakeHeight(InPosition, static_cast<double>(SyncTime));
		}
	}

	return Height;
}

float USWRippleWaterWaves::GetWaveAttenuationFactor(const FVector& InPosition, float InWaterDepth, float InMinDepth) const
{
	if (BaseWavesAsset)
	{
		if (const UWaterWaves* ActualWaves = BaseWavesAsset->GetWaterWaves())
		{
			return ActualWaves->GetWaveAttenuationFactor(InPosition, InWaterDepth, InMinDepth);
		}
	}
	return 1.0f;
}

const UWaterWaves* USWRippleWaterWaves::GetWaterWaves() const
{
	return BaseWavesAsset ? BaseWavesAsset->GetWaterWaves() : nullptr;
}

UWaterWaves* USWRippleWaterWaves::GetWaterWaves()
{
	return BaseWavesAsset ? BaseWavesAsset->GetWaterWaves() : nullptr;
}
