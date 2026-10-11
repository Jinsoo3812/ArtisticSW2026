#pragma once

#include "CoreMinimal.h"
#include "SWShipWakeTypes.h"

class UWorld;

class WATERANDSHIP_API FSWShipWakeRenderingDiagnostics
{
public:
	static bool IsEnabled();
	static int32 GetRenderEventMode();
	static void FilterRenderingEvents(TArray<FSWShipWakeEvent>& Events);
	void Update(const UWorld* World, double ServerTime, const FVector2D& GridCenter,
		TConstArrayView<FSWShipWakeEvent> RenderingEvents, ESWKelvinFroudeProfile Profile);

private:
	TArray<FSWShipWakeEvent> PreviousEvents;
	FVector2D ProbeCenter = FVector2D::ZeroVector;
	float PreviousHeights[9] = {};
	double PreviousTime = -1.0;
	double LastLogTime = -1.0;
	int32 LastReset = 0;
	int32 LastMode = -1;
	ESWKelvinFroudeProfile LastProfile = ESWKelvinFroudeProfile::Fr_0_50;
	float LastSpacing = 0.0f;
	float MaxSetDelta = 0.0f;
	float MaxFrameDelta = 0.0f;
	FString SetEvidence;
	FString FrameEvidence;
	FString PreviousFrameEvidence;
};
