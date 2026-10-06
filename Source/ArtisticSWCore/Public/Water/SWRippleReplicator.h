#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Room/SWVoyageResetParticipant.h"
#include "Water/SWRippleTypes.h"
#include "SWRippleReplicator.generated.h"

class ASWRippleReplicator;

USTRUCT()
struct ARTISTICSWCORE_API FSWReplicatedRippleItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	FSWRippleEvent Event;
};

USTRUCT()
struct ARTISTICSWCORE_API FSWReplicatedRippleArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSWReplicatedRippleItem> Items;

	ASWRippleReplicator* Owner = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FastArrayDeltaSerialize<FSWReplicatedRippleItem, FSWReplicatedRippleArray>(Items, DeltaParams, *this);
	}

	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize);
};

template<>
struct TStructOpsTypeTraits<FSWReplicatedRippleArray> : public TStructOpsTypeTraitsBase2<FSWReplicatedRippleArray>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};

UCLASS(NotPlaceable, Transient)
class ARTISTICSWCORE_API ASWRippleReplicator : public AActor, public ISWVoyageResetParticipant
{
	GENERATED_BODY()

public:
	ASWRippleReplicator();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	bool AddServerRipple(
		const FVector2D& Origin,
		float InitialAmplitude,
		float WaveSpeed,
		float DecayRate,
		float WaveLength);

	void ApplyReplicatedEvent(const FSWRippleEvent& Event) const;
	void ResetForVoyage(int32 Generation);
	virtual ESWVoyagePolicy GetVoyagePolicy_Implementation() const override { return ESWVoyagePolicy::ResetParticipant; }
	virtual FName GetVoyageParticipantId_Implementation() const override;
	virtual ESWVoyageStepResult PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;

private:
	UPROPERTY(Replicated) int32 VoyageGeneration = 0;
	UPROPERTY(Replicated)
	FSWReplicatedRippleArray ReplicatedRipples;

	int32 NextEventId = 1;
	void RemoveExpiredActiveEvents(double ServerTime);
};
