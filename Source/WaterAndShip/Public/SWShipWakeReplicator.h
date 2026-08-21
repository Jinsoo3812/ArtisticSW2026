#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "SWShipWakeTypes.h"
#include "SWShipWakeReplicator.generated.h"

class ASWShipWakeReplicator;

USTRUCT()
struct WATERANDSHIP_API FSWReplicatedShipWakeItem : public FFastArraySerializerItem
{
	GENERATED_BODY()
	UPROPERTY() FSWShipWakeEvent Event;
};

USTRUCT()
struct WATERANDSHIP_API FSWReplicatedShipWakeArray : public FFastArraySerializer
{
	GENERATED_BODY()
	UPROPERTY() TArray<FSWReplicatedShipWakeItem> Items;
	ASWShipWakeReplicator* Owner = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FastArrayDeltaSerialize<FSWReplicatedShipWakeItem, FSWReplicatedShipWakeArray>(
			Items, DeltaParams, *this);
	}
	void PostReplicatedAdd(TArrayView<int32> AddedIndices, int32 FinalSize);
	void PostReplicatedChange(TArrayView<int32> ChangedIndices, int32 FinalSize);
};

template<> struct TStructOpsTypeTraits<FSWReplicatedShipWakeArray>
	: public TStructOpsTypeTraitsBase2<FSWReplicatedShipWakeArray>
{
	enum { WithNetDeltaSerializer = true };
};

UCLASS(NotPlaceable, Transient)
class WATERANDSHIP_API ASWShipWakeReplicator : public AActor
{
	GENERATED_BODY()

public:
	ASWShipWakeReplicator();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool AddServerEvent(const FSWShipWakeEvent& EventTemplate);
	void ApplyReplicatedEvent(const FSWShipWakeEvent& Event) const;

private:
	void RemoveExpired(double ServerTime);
	UPROPERTY(Replicated) FSWReplicatedShipWakeArray ReplicatedEvents;
	int32 NextEventId = 1;
};
