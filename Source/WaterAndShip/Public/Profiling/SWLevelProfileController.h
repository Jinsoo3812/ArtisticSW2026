#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWLevelProfileController.generated.h"

/**
 * Starts a bounded CSV capture after the requested gameplay world has settled.
 * Spawned transiently by URippleSubsystem only with -SWProfileLevel.
 */
UCLASS()
class WATERANDSHIP_API ASWLevelProfileController : public AActor
{
	GENERATED_BODY()

public:
	ASWLevelProfileController();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	float WarmupSeconds = 5.0f;
	int32 CaptureFrames = 600;
	bool bAutoQuit = false;
	bool bCaptureRequested = false;
	bool bObservedCapture = false;
	bool bScenarioApplied = false;
	int32 EnemyShipLimit = -1;
	bool bDisableEnemyOverlaps = false;
	bool bDisableEnemyRootOverlaps = false;
	bool bDisableEnemyShipShadows = false;
	bool bProfileGPU = false;
	bool bScreenshot = false;
	FString ScreenshotName;
	bool bFixedWaterCamera = false;
	float FixedCameraZOffset = 400.0f;
	float FixedCameraPitch = -18.0f;
	bool bInjectRipple = false;
	bool bRippleInjected = false;
	float RippleLeadSeconds = 0.75f;
	float RippleForwardDistance = 3000.0f;
	float RippleAmplitude = 100.0f;
	double BeginWorldTime = 0.0;
	uint32 NetworkStartInBytes = 0;
	uint32 NetworkStartOutBytes = 0;
	uint32 NetworkStartInPackets = 0;
	uint32 NetworkStartOutPackets = 0;
	uint32 NetworkStartInBunches = 0;
	uint32 NetworkStartOutBunches = 0;
	uint64 StartUsedPhysicalBytes = 0;

	void ApplyProfileScenario();
	void InjectProfileRipple();
	void CaptureProcessAndNetworkStart();
	void LogProcessAndNetworkEnd() const;
};
