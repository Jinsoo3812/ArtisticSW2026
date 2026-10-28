#pragma once

#include "CoreMinimal.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Subsystems/WorldSubsystem.h"
#include "Audio/SWFoleyTypes.h"
#include "SWFoleySubsystem.generated.h"

class USWFoleyComponent;
class USWFoleyAudioProfile;
class USoundAttenuation;
class USoundConcurrency;
struct FStreamableHandle;

struct FSWFoleyStats
{
	uint64 Submitted = 0, Played = 0, DistanceRejected = 0, NotReady = 0;
	uint64 DuplicateRejected = 0, CapacityRejected = 0, Expired = 0, BudgetDropped = 0, SurfaceTraces = 0;
	uint64 Unmapped = 0, EventFallbacks = 0, LoopRejected = 0;
	int32 Pending = 0;
	FString LastSound;
	EPhysicalSurface LastSurface = SurfaceType_Default;
	double LastMilliseconds = 0.0;
};

/** Event-driven, listener-aware client scheduler. Post-actor batching never builds an audio backlog. */
UCLASS()
class CLASSFEATURE_API USWFoleySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldEndPlay(UWorld& World) override;
	bool Submit(USWFoleyComponent* Component, const FSWFoleyEvent& Event);
	bool PrepareLocalAudio(USWFoleyComponent* Component);
	void Cancel(USWFoleyComponent* Component);
	const FSWFoleyStats& GetStats() const { return Stats; }
	/** Shared per-world budget for nearby airborne NPC water-entry queries. */
	bool AdmitWaterProbe(const FVector& Location);
protected:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
private:
	friend class FSWFoleyAdmissionTest;
	friend class FSWFoleySurfaceTraceTest;
	friend class FSWFoleyLifetimeTest;
	struct FRequest
	{
		TWeakObjectPtr<USWFoleyComponent> Component;
		FSWFoleyEvent Event;
		FSoftObjectPath Profile;
		double SubmittedAt = 0;
		float Score = 0;
		bool bLocal = false;
		uint64 Order = 0;
	};
	struct FResources
	{
		TSharedPtr<FStreamableHandle> ProfileLoad;
		TSharedPtr<FStreamableHandle> SoundsLoad;
		double LastUsed = 0;
		bool bPrepared = false;
	};
	static constexpr int32 MaxPending = 128;
	static constexpr int32 MaxProfiles = 32;
	static bool ValidateEvent(const FSWFoleyEvent& Event, float MaxAge);
	static float ScoreEvent(float Importance, float DistanceSquared, float Radius);
	static bool HigherPriority(const FRequest& A, const FRequest& B);
	static TArray<FRequest> SelectBatch(TArray<FRequest>& Requests, double Now, float MaxAge,
		int32 LocalLimit, int32 RemoteLimit, uint64& OutExpired, uint64& OutDropped);
	void OnPostActorTick(UWorld* World, ELevelTick TickType, float DeltaTime);
	void RefreshListeners();
	bool GetListenerDistanceSquared(const FVector& Location, float& OutDistanceSquared);
	USWFoleyAudioProfile* ResolveReadyProfile(const FSoftObjectPath& Path);
	void PrepareProfile(const FSoftObjectPath& Path);
	void Execute(const FRequest& Request);
	void Stop();
	TArray<FRequest> Pending;
	TArray<FVector> ListenerPositions;
	TMap<FSoftObjectPath, FResources> Resources;
	UPROPERTY(Transient)
	TMap<FSoftObjectPath, TObjectPtr<USWFoleyAudioProfile>> LoadedProfiles;
	UPROPERTY(Transient)
	TMap<FSoftObjectPath, TObjectPtr<USoundAttenuation>> FallbackAttenuation;
	UPROPERTY(Transient)
	TObjectPtr<USoundConcurrency> LocalConcurrency;
	UPROPERTY(Transient)
	TObjectPtr<USoundConcurrency> RemoteConcurrency;
	FDelegateHandle PostActorTickHandle;
	uint64 ListenerFrame = MAX_uint64;
	uint64 NextOrder = 0;
	uint64 WaterProbeFrame = MAX_uint64;
	int32 WaterProbeCount = 0;
	double RemoteTokens = 12.0;
	double LastTokenTime = -1.0;
	bool bStopped = false;
	FSWFoleyStats Stats;
};
