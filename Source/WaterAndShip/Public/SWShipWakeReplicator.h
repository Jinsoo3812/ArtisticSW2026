#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Room/SWVoyageResetParticipant.h"
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
class WATERANDSHIP_API ASWShipWakeReplicator : public AActor, public ISWVoyageResetParticipant
{
	GENERATED_BODY()

public:
	ASWShipWakeReplicator();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool AddServerEvent(const FSWShipWakeEvent& EventTemplate);
	void ApplyReplicatedEvent(const FSWShipWakeEvent& Event) const;
	void ResetForVoyage(int32 Generation);
	virtual ESWVoyagePolicy GetVoyagePolicy_Implementation() const override { return ESWVoyagePolicy::ResetParticipant; }
	virtual FName GetVoyageParticipantId_Implementation() const override;
	virtual ESWVoyageStepResult PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;

private:
	UPROPERTY(Replicated) int32 VoyageGeneration = 0;
	void RemoveExpired(double ServerTime);
	UPROPERTY(Replicated) FSWReplicatedShipWakeArray ReplicatedEvents;
	int32 NextEventId = 1;
};
