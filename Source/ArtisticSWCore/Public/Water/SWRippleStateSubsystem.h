#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Water/SWRippleTypes.h"
#include "SWRippleStateSubsystem.generated.h"

class ASWRippleReplicator;

UCLASS()
class ARTISTICSWCORE_API USWRippleStateSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	bool SubmitAuthoritativeRipple(
		const FVector2D& Origin,
		float InitialAmplitude,
		float WaveSpeed,
		float DecayRate,
		float WaveLength);

	void AddOrUpdateReplicatedEvent(const FSWRippleEvent& Event);
	void GetEventsSnapshot(TArray<FSWRippleEvent>& OutEvents) const;
	void GetActiveEventsSnapshot(double ServerTime, TArray<FSWRippleEvent>& OutEvents) const;
	float GetRippleHeight(const FVector& Location, double ServerTime) const;
	int32 GetEventCount() const;
	uint32 GetRevision() const { return Revision.Load(); }

	void RegisterReplicator(ASWRippleReplicator* InReplicator);
	ASWRippleReplicator* GetReplicator() const { return Replicator.Get(); }
	double GetServerTime() const;

	/** Extra CPU retention after visual expiry so Network Physics can resimulate recent frames. */
	UPROPERTY(EditAnywhere, Category = "Water Ripple|Network", meta = (ClampMin = "0.0", Units = "s"))
	float PhysicsHistoryRetentionSeconds = 2.0f;

private:
	mutable FRWLock EventsLock;
	TArray<FSWRippleEvent> Events;
	TAtomic<uint32> Revision { 0 };
	TWeakObjectPtr<ASWRippleReplicator> Replicator;
};

