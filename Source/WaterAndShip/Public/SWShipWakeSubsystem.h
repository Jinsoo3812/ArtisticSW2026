#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SWShipWakeTypes.h"
#include "SWShipWakeSubsystem.generated.h"

class ASWShipWakeReplicator;
class UMaterialInstanceDynamic;
class UTexture2D;

/** M7 Ripple-style immutable event store and CPU/GPU bridge. */
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

	static constexpr int32 MaxWakeCapacity = 1024;
	static constexpr int32 DefaultWakeCapacity = 1024;
	static constexpr int32 WakeCapacity = 1024;

	/** Returns dynamic maximum buffer capacity controlled by sw.ShipWake.MaxCapacity CVar and cached. */
	static int32 GetMaxCapacity();

private:
	void AddOrUpdateCapped(const FSWShipWakeEvent& Event);
	void RemoveExpiredEvents(double ServerTime);
	void UpdateEventTexture();
	void RefreshWaterMaterials();
	void BindToWaterMaterials(double ServerTime);
	UTexture2D* GetActiveGoldenTexture() const;

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

	TWeakObjectPtr<ASWShipWakeReplicator> Replicator;
	TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> WaterMaterials;

	UPROPERTY(Transient) TObjectPtr<UTexture2D> EventTexture;
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> GoldenTextures;
};
