#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "SWShipWakeTypes.h"
#include "SWShipWakeSubsystem.generated.h"

/**
 * Authoritative CPU wake cache plus a compact material parameter texture.
 *
 * This mirrors RippleSubsystem's proven split: water/physics queries use the
 * thread-safe event cache while the water material evaluates the same packets
 * from a small float texture. It never reads a GPU height texture back to CPU.
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

	static constexpr int32 WakeCapacity = 64;

private:
	void RemoveExpiredEvents(double ServerTime);
	void UpdateTexture(double ServerTime);
	void BindToWaterMaterials(double ServerTime);

	mutable FRWLock EventsLock;
	TArray<FSWShipWakeEvent> Events;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> WakeTexture;

	int32 LastUploadedCount = 0;
	float PhysicsHistoryRetentionSeconds = 2.0f;
};

