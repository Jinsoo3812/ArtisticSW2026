#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "Engine/Texture2D.h"
#include "RippleSubsystem.generated.h"

USTRUCT(BlueprintType)
struct FWaveData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Wave")
	FVector2D Origin = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Wave")
	float StartTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Wave")
	float InitialAmplitude = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Wave")
	float WaveSpeed = 300.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Wave")
	float DecayRate = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Wave")
	float WaveLength = 100.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Wave")
	float ExpireTime = 0.0f;
};

UCLASS(BlueprintType, Blueprintable)
class CLASSFEATURE_API URippleSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	URippleSubsystem();

	// UWorldSubsystem implementation
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject implementation
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickable() const override { return !IsTemplate(); }
	virtual TStatId GetStatId() const override;

	// Add a ripple. Thread-safe writing.
	UFUNCTION(BlueprintCallable, Category = "Water Ripple")
	void AddRipple(FVector2D Origin, float InitialAmplitude, float WaveSpeed = 300.0f, float DecayRate = 1.0f, float WaveLength = 100.0f);

	// Get total ripple height at a given location. Thread-safe reading.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Water Ripple")
	float GetRippleHeight(const FVector& Location) const;

	// Get the GPU-accessible texture
	UFUNCTION(BlueprintCallable, Category = "Water Ripple")
	UTexture2D* GetRippleTexture() const { return RippleTexture; }

	// Distance from any player within which ripples can be generated
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Ripple")
	float MaxGenerationDistance = 10000.0f; // 100m

	// Minimum amplitude threshold below which ripples are culled
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Ripple")
	float AmplitudeCullThreshold = 0.01f;

	// Max number of active ripples
	static const int32 MaxActiveRipples = 32;

private:
	// Dynamic texture updated every frame (PF_A32B32G32R32F, 32x2)
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> RippleTexture;

	// Active ripples array
	TArray<FWaveData> ActiveRipples;

	// Multi-threading read/write lock for ActiveRipples
	mutable FRWLock RipplesLock;

	// Updates the GPU texture with the active ripples data
	void UpdateTexture();

	// Synchronize Server Time to the Material Parameter Collection
	void UpdateServerTimeMPC(float ServerTime);
};
