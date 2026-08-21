#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RippleSubsystem.generated.h"

/**
 * Server-side ripple detector and client-side ripple renderer.
 * Authoritative ripple state/query math lives in USWRippleStateSubsystem.
 */
UCLASS(BlueprintType, Blueprintable)
class WATERANDSHIP_API URippleSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	URippleSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickable() const override { return !IsTemplate(); }
	virtual TStatId GetStatId() const override;

	/** Submits an authoritative ripple. Calls on clients are rejected. */
	UFUNCTION(BlueprintCallable, Category = "Water Ripple")
	void AddRipple(FVector2D Origin, float InitialAmplitude, float WaveSpeed = 300.0f, float DecayRate = 1.0f, float WaveLength = 100.0f);

	/** Creates a client-only visual prediction that is reconciled by the replicated server event. */
	void AddPredictedRippleFromImpact(FVector2D Origin, float DownwardSpeed);

	/** Evaluates the server-authored ripple cache at synchronized server time. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Water Ripple")
	float GetRippleHeight(const FVector& Location) const;

	UFUNCTION(BlueprintCallable, Category = "Water Ripple")
	UTexture2D* GetRippleTexture() const { return RippleTexture; }

	/** GPU-baked ripple height/normal field consumed by water materials. */
	UTextureRenderTarget2D* GetRippleRenderTarget() const { return RippleRenderTarget; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Ripple")
	float MaxGenerationDistance = 10000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Ripple")
	float AmplitudeCullThreshold = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Ripple|Parameters")
	float DefaultWaveSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Ripple|Parameters")
	float DefaultDecayRate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Ripple|Parameters")
	float DefaultWaveLength = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Ripple|Parameters")
	float AmplitudeMultiplier = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Ripple|Parameters")
	float MaxInitialAmplitude = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Ripple|Parameters")
	float MinVelocityThreshold = 100.0f;

private:
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> RippleTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RippleRenderTarget;

	int32 RippleCapacity = 32;
	int32 RippleRenderTargetResolution = 512;
	float RippleGridSizeCm = 20000.0f;
	FVector2D CurrentRippleGridCenter = FVector2D::ZeroVector;

	void UpdateTexture();
	void CreateRippleRenderTarget();
	FVector2D ResolveRippleGridCenter() const;
	void DispatchRippleComputeShader(double ServerTime);
	void BindRippleDataToWaterMaterials();
	double GetServerTime() const;

	UFUNCTION()
	void OnWaterBodyActorOverlap(AActor* OverlappedActor, AActor* OtherActor);

	bool bDiagnosticsEnabled = false;
	float DiagnosticsStartTime = 0.0f;
	float DiagnosticsLastSummaryTime = -1.0f;
	int32 DiagnosticsLastUploadedRippleCount = INDEX_NONE;
	bool bDiagnosticsLastTextureResourceValid = false;
	bool bHasUploadedStateRevision = false;
	uint32 LastUploadedStateRevision = 0;
	int32 LastUploadedRippleCount = 0;
	double NextTextureTransitionServerTime = TNumericLimits<double>::Max();

	void TickDiagnostics();
};
