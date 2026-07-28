#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Water/SWRippleProfile.h"
#include "SWRippleProfileController.generated.h"

class USWRippleStateSubsystem;

/**
 * Deterministic R0/R8/R32 driver for KKH_Profile_Ripple.
 * It does not optimize gameplay code; it only creates controlled events, runs a
 * fixed query grid, emits trace bookmarks, and logs cumulative metric deltas.
 */
UCLASS()
class WATERANDSHIP_API ASWRippleProfileController : public AActor
{
	GENERATED_BODY()

public:
	ASWRippleProfileController();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(EditAnywhere, Category = "SW Profile|Ripple", meta = (ClampMin = "0", ClampMax = "32"))
	int32 RippleCount = 8;

	UPROPERTY(EditAnywhere, Category = "SW Profile|Ripple", meta = (ClampMin = "0"))
	int32 QueriesPerFrame = 256;

	UPROPERTY(EditAnywhere, Category = "SW Profile|Timing", meta = (ClampMin = "0.0", Units = "s"))
	float WarmupSeconds = 14.0f;

	UPROPERTY(EditAnywhere, Category = "SW Profile|Timing", meta = (ClampMin = "0.0", Units = "s"))
	float ReplicationSettleSeconds = 1.0f;

	UPROPERTY(EditAnywhere, Category = "SW Profile|Timing", meta = (ClampMin = "0.1", Units = "s"))
	float MeasurementSeconds = 7.0f;

	UPROPERTY(EditAnywhere, Category = "SW Profile|Layout", meta = (ClampMin = "0.0", Units = "cm"))
	float RippleSpawnRadius = 800.0f;

	UPROPERTY(EditAnywhere, Category = "SW Profile|Layout", meta = (ClampMin = "100.0", Units = "cm"))
	float QueryAreaHalfExtent = 3000.0f;

	UPROPERTY(EditAnywhere, Category = "SW Profile|Ripple", meta = (ClampMin = "0.01", Units = "cm"))
	float InitialAmplitude = 100.0f;

	UPROPERTY(EditAnywhere, Category = "SW Profile|Ripple", meta = (ClampMin = "0.0"))
	float WaveSpeed = 300.0f;

	UPROPERTY(EditAnywhere, Category = "SW Profile|Ripple", meta = (ClampMin = "0.01"))
	float DecayRate = 0.1f;

	UPROPERTY(EditAnywhere, Category = "SW Profile|Ripple", meta = (ClampMin = "1.0", Units = "cm"))
	float WaveLength = 100.0f;

	enum class EPhase : uint8
	{
		Warmup,
		ReplicationSettle,
		Measure,
		Complete,
	};

	EPhase Phase = EPhase::Warmup;
	double PhaseStartWorldTime = 0.0;
	bool bAutoQuit = false;
	int32 AcceptedRippleCount = 0;
	float QueryResultSink = 0.0f;
	TArray<FVector> QueryPositions;
	FSWRippleProfileSnapshot MeasurementStartSnapshot;
	bool bNetworkSnapshotCaptured = false;
	uint32 NetworkStartInBytes = 0;
	uint32 NetworkStartOutBytes = 0;
	uint32 NetworkStartInPackets = 0;
	uint32 NetworkStartOutPackets = 0;
	uint32 NetworkStartInBunches = 0;
	uint32 NetworkStartOutBunches = 0;

	void ParseCommandLineOverrides();
	void BuildQueryPositions();
	void BeginRippleScenario();
	void CaptureNetworkSnapshot();
	void BeginMeasurement();
	void RunQueryBatch();
	void CompleteMeasurement();
	void LogMetricDelta(const FSWRippleProfileSnapshot& EndSnapshot) const;
	double GetWorldTime() const;
	const TCHAR* GetRoleName() const;
};
