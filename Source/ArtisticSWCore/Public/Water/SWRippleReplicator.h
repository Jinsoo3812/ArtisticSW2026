#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Net/Serialization/FastArraySerializer.h"
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
class ARTISTICSWCORE_API ASWRippleReplicator : public AActor
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

private:
	UPROPERTY(Replicated)
	FSWReplicatedRippleArray ReplicatedRipples;

	int32 NextEventId = 1;
	void RemoveExpiredActiveEvents(double ServerTime);
};
