#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SWShipWakeTypes.h"
#include "SWShipWakeSubsystem.generated.h"

class ASWShipWakeReplicator;
class UMaterialInstanceDynamic;
class UTexture2D;
class UTextureRenderTarget2D;

/** M7 Ripple-style immutable event store and CPU/GPU bridge with Compute Shader baking. */
UCLASS()
class WATERANDSHIP_API USWShipWakeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	bool SubmitAuthoritativeEvent(const FSWShipWakeEvent& EventTemplate);
	bool SubmitPredictedEvent(const FSWShipWakeEvent& EventTemplate);
	void AddOrUpdateReplicatedEvent(const FSWShipWakeEvent& Event);
	void RegisterReplicator(ASWShipWakeReplicator* InReplicator);

	void GetEventsSnapshot(TArray<FSWShipWakeEvent>& OutEvents) const;
	void GetActiveEventsSnapshot(double ServerTime, TArray<FSWShipWakeEvent>& OutEvents) const;
	float GetWakeHeight(const FVector& WorldPosition, double ServerTime) const;
	FVector2D GetWakeGradient(const FVector& WorldPosition, double ServerTime) const;
	double GetServerTime() const;
	int32 GetEventCount() const;

	void SetActiveFroudeProfile(ESWKelvinFroudeProfile Profile);
	ESWKelvinFroudeProfile GetActiveFroudeProfile() const;

	UFUNCTION(BlueprintCallable, Category = "Ship Wake")
	UTextureRenderTarget2D* GetWakeRenderTarget() const { return WakeRenderTarget; }
	UTextureRenderTarget2D* GetWakeFoamSourceRenderTarget() const { return WakeFoamSourceRenderTarget; }
	FVector2D GetWakeGridCenter() const { return CurrentGridCenter; }
	float GetWakeGridSize() const { return GridSizeCm; }

	static constexpr int32 MaxWakeCapacity = 256;
	static constexpr int32 DefaultWakeCapacity = 256;
	static constexpr int32 WakeCapacity = 256;

	/** Returns dynamic maximum buffer capacity controlled by sw.ShipWake.MaxCapacity CVar and cached. */
	static int32 GetMaxCapacity();

private:
	void AddOrUpdateCapped(const FSWShipWakeEvent& Event);
	void RemoveExpiredEvents(double ServerTime);
	void UpdateEventTexture();
	void RefreshWaterMaterials();
	void BindToWaterMaterials(double ServerTime);
	UTexture2D* GetActiveGoldenTexture() const;

	void CreateWakeRenderTarget();
	void DispatchWakeComputeShader(double ServerTime);
	FVector2D ResolveWakeGridCenter() const;

	mutable FRWLock EventsLock;
	TArray<FSWShipWakeEvent> Events;
	TAtomic<uint32> Revision { 0 };
	uint32 UploadedRevision = MAX_uint32;
	int32 LastUploadedCount = 0;
	int32 NextPredictedEventId = -1;
	float PhysicsHistoryRetentionSeconds = 2.0f;
	float MaterialRefreshAccumulator = 0.0f;
	ESWKelvinFroudeProfile ActiveFroudeProfile = ESWKelvinFroudeProfile::Fr_0_50;
	FVector2D LockedProbeLocation = FVector2D::ZeroVector;
	bool bHasLockedProbe = false;

	FVector2D CurrentGridCenter = FVector2D::ZeroVector;
	float GridSizeCm = 30000.0f; // 300 meters coverage
	int32 RenderTargetResolution = 512;

	TWeakObjectPtr<ASWShipWakeReplicator> Replicator;
	TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> WaterMaterials;

	UPROPERTY(Transient) TObjectPtr<UTexture2D> EventTexture;
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> GoldenTextures;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> WakeRenderTarget;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> WakeFoamSourceRenderTarget;
};
