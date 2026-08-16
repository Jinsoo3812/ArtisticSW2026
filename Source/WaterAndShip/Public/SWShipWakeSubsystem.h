#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "SWShipWakeTypes.h"
#include "SWShipWakeSubsystem.generated.h"

class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;

/**
 * Authoritative CPU wake cache, compact source texture and M3 signed-height field.
 *
 * Water/physics queries use the thread-safe event cache and the M2 analytic
 * approximation. The visual surface integrates the same live hull sources into
 * a persistent triple-buffer GPU field. Exact GPU/CPU parity is deferred until
 * an asynchronous field readback or shared lower-resolution solver is added.
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
	UTextureRenderTarget2D* GetHeightField() const;

	static constexpr int32 WakeCapacity = 64;

private:
	void RemoveExpiredEvents(double ServerTime);
	void UpdateTexture(double ServerTime);
	void BindToWaterMaterials(double ServerTime);
	bool InitializeHeightField();
	void StepHeightField(double ServerTime, float DeltaTime);
	FVector2D ResolveDesiredFieldCenter(double ServerTime) const;
	UTextureRenderTarget2D* CreateHeightRenderTarget(const FName& Name);

	mutable FRWLock EventsLock;
	TArray<FSWShipWakeEvent> Events;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> WakeTexture;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> HeightStates;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeightFieldUpdateMID;

	TArray<FVector2D> HeightStateCenters;
	int32 PreviousHeightStateIndex = 0;
	int32 CurrentHeightStateIndex = 1;
	int32 NextHeightStateIndex = 2;
	float HeightFieldAccumulator = 0.0f;
	bool bHeightFieldInitialized = false;

	int32 LastUploadedCount = 0;
	float PhysicsHistoryRetentionSeconds = 2.0f;
};

