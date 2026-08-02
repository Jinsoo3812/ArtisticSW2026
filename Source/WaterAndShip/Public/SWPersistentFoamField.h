#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWPersistentFoamField.generated.h"

class AWaterBody;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;
class UTextureRenderTarget2D;

/**
 * Maintains a local persistent ocean-foam field in two ping-pong render targets.
 *
 * The update material owns generation, velocity backtracing and decay. This
 * actor only supplies the current water body, field transform and frame delta,
 * then exposes the latest state to the water material instance. It is a local
 * visual system and intentionally has no gameplay or replication authority.
 */
UCLASS(BlueprintType)
class WATERANDSHIP_API ASWPersistentFoamField : public AActor
{
	GENERATED_BODY()

public:
	ASWPersistentFoamField();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "ArtisticSW|Water|Foam")
	UTextureRenderTarget2D* GetCurrentFoamState() const;

	UFUNCTION(BlueprintCallable, Category = "ArtisticSW|Water|Foam")
	void ResetFoamState();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Ocean whose Water Body Index and material instance drive this field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foam|Binding")
	TObjectPtr<AWaterBody> TargetWaterBody;

	/** Unlit canvas material that performs one persistent-foam update. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foam|Binding")
	TObjectPtr<UMaterialInterface> FoamStateUpdateMaterial;

	/** Texture resolution per side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foam|Domain", meta = (ClampMin = 128, ClampMax = 2048))
	int32 Resolution = 1024;

	/** Width and height of the square field in centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foam|Domain", meta = (ClampMin = 1000.0))
	float FieldWorldSizeCm = 30000.0f;

	/** Keeps the local field centered on the first local player's camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foam|Domain")
	bool bFollowLocalView = true;

	/** Seconds for persistent foam density to fall to roughly 37 percent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foam|Lifetime", meta = (ClampMin = 0.1))
	float FoamLifetimeSeconds = 5.5f;

	/** Rate at which the current Gerstner breaking source fills the state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foam|Generation", meta = (ClampMin = 0.0))
	float SourceRate = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foam|Generation")
	float CrestStartCm = 28.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foam|Generation")
	float CrestEndCm = 86.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foam|Generation")
	float SlopeStart = 0.026f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foam|Generation")
	float SlopeEnd = 0.115f;

	/** Resolution of the CPU Gerstner source/velocity field. The persistent state remains at Resolution. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foam|Generation", meta = (ClampMin = 32, ClampMax = 512))
	int32 SourceResolution = 128;

	/** CPU source refresh interval. 0.1 seconds is sufficient because the GPU state advects every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foam|Generation", meta = (ClampMin = 0.016, ClampMax = 1.0))
	float SourceUpdateIntervalSeconds = 0.1f;

	/** Scale applied to exact per-frame Gerstner horizontal displacement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foam|Advection", meta = (ClampMin = 0.0, ClampMax = 4.0))
	float AdvectionScale = 1.0f;

	/** Symmetric range used to encode exact Gerstner horizontal velocity into the CPU source texture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foam|Advection", meta = (ClampMin = 10.0, ClampMax = 10000.0))
	float MaxEncodedVelocityCmPerSecond = 1200.0f;

	/** Development-only one-shot state readback used to diagnose a V5 test level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foam|Debug")
	bool bLogInitialStateStatistics = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Foam|Runtime")
	TObjectPtr<UTextureRenderTarget2D> FoamStateA;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Foam|Runtime")
	TObjectPtr<UTextureRenderTarget2D> FoamStateB;

private:
	bool InitializeFoamField();
	void ResolveTargetWaterBody();
	void PushStateToWaterMaterial(UTextureRenderTarget2D* State);
	void LogInitialStateStatistics(UTextureRenderTarget2D* State);
	bool CreateCpuWaveField();
	bool UpdateCpuWaveField();
	FVector2D ResolveDesiredCenter() const;
	UTextureRenderTarget2D* CreateStateRenderTarget(const FName Name);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FoamUpdateMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WaterMID;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> CpuWaveField;

	FVector2D CurrentCenter = FVector2D::ZeroVector;
	FVector2D PreviousCenter = FVector2D::ZeroVector;
	FVector2D CpuWaveFieldCenter = FVector2D::ZeroVector;
	float SourceUpdateElapsedSeconds = 0.0f;
	bool bLatestStateIsA = true;
	bool bInitialized = false;
	bool bInitialStateStatisticsLogged = false;
	float InitialStateStatisticsElapsedSeconds = 0.0f;
};
