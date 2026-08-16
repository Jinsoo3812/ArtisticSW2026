#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "SWShipWakeTypes.h"
#include "SWShipWakeSubsystem.generated.h"

/**
 * M6 persistent dispersive wake field. A CPU-authoritative spectral deep-water
 * solver advances the signed height field while three spatial states are
 * uploaded for identical rendering and buoyancy sampling.
 */
UCLASS()
class WATERANDSHIP_API USWShipWakeSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickable() const override { return !IsTemplate(); }
	virtual TStatId GetStatId() const override;

	void AddOrUpdateEvent(const FSWShipWakeEvent& Event);
	void GetEventsSnapshot(TArray<FSWShipWakeEvent>& OutEvents) const;
	void GetActiveEventsSnapshot(double ServerTime, TArray<FSWShipWakeEvent>& OutEvents) const;
	float GetWakeHeight(const FVector& WorldPosition, double ServerTime) const;
	FVector2D GetWakeGradient(const FVector& WorldPosition, double ServerTime) const;
	double GetServerTime() const;
	int32 GetEventCount() const;

	UTexture2D* GetWakeTexture() const { return WakeTexture; }
	UTexture2D* GetTrajectoryTexture() const { return TrajectoryTexture; }
	UTexture2D* GetKelvinAtlasTexture() const { return KelvinAtlasTexture; }
	UTexture2D* GetHeightField() const;
	UTexture2D* GetPreviousHeightField() const;

	static constexpr int32 WakeCapacity = 64;
	static constexpr int32 WakeSampleCapacity = WakeCapacity * 8;
	static constexpr int32 TrajectoryCapacity = 16;

private:
	void RemoveExpiredEvents(double ServerTime);
	void UpdateTexture(double ServerTime);
	void BindToWaterMaterials(double ServerTime);
	bool InitializeHeightHistory();
	void ResetHeightHistory();
	void StepHeightHistory(double ServerTime, float DeltaTime);
	void UploadHeightHistoryState(int32 StateIndex);
	float SampleHeightHistory(const FVector2D& WorldPosition) const;
	float GetEffectiveHistoryAlpha() const;
	void LogRuntimeDiagnostics(double ServerTime, float FrameDeltaTime);
	FVector2D ResolveDesiredFieldCenter(double ServerTime) const;

	mutable FRWLock EventsLock;
	/** Immutable, time-ordered-in-evaluation emitter samples. Multiple entries
	 * with the same EventId are retained so a fixed simulation step can resolve
	 * the state that existed at its own server time instead of seeing a future
	 * replacement or an empty source. */
	TArray<FSWShipWakeEvent> Events;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> WakeTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TrajectoryTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> KelvinAtlasTexture;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> HeightHistoryTextures;

	mutable FRWLock HeightHistoryLock;
	TArray<TArray<float>> HeightHistoryValues;
	TArray<FVector2D> HeightHistoryCenters;
	/** Complex Fourier coefficients: X=real, Y=imaginary. */
	TArray<FVector2f> SpectralHeight;
	TArray<FVector2f> SpectralVelocity;
	FVector2D SpectralFieldCenter = FVector2D::ZeroVector;
	TArray<FSWShipWakeEvent> PreviousSolverEvents;
	int32 PreviousHeightStateIndex = 0;
	int32 CurrentHeightStateIndex = 1;
	int32 NextHeightStateIndex = 2;
	float HeightFieldAccumulator = 0.0f;
	float HeightHistoryInterpolationAlpha = 0.0f;
	bool bHeightFieldInitialized = false;
	int32 HeightFieldResolution = 0;
	float HeightFieldWorldSizeCm = 0.0f;

	struct FHeightHistoryRuntimeStats
	{
		double StepMilliseconds = 0.0;
		float TargetMinimum = 0.0f;
		float TargetMaximum = 0.0f;
		float TargetRms = 0.0f;
		float OutputMinimum = 0.0f;
		float OutputMaximum = 0.0f;
		float OutputRms = 0.0f;
		float MaximumStepDelta = 0.0f;
		float SaturatedFraction = 0.0f;
		FVector2D CenterDelta = FVector2D::ZeroVector;
		int32 ActiveEventCount = 0;
		double EvaluationServerTime = 0.0;
	};

	FHeightHistoryRuntimeStats RuntimeStats;
	double LastRuntimeLogServerTime = -DBL_MAX;

	int32 LastUploadedCount = 0;
	float PhysicsHistoryRetentionSeconds = 2.0f;
};

