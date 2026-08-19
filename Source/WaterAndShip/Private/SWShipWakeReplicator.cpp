#include "SWShipWakeReplicator.h"

#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "SWShipWakeSubsystem.h"

void FSWReplicatedShipWakeArray::PostReplicatedAdd(
	const TArrayView<int32> AddedIndices, const int32 FinalSize)
{
	if (!Owner) return;
	for (const int32 Index : AddedIndices)
	{
		if (Items.IsValidIndex(Index)) Owner->ApplyReplicatedEvent(Items[Index].Event);
	}
}

void FSWReplicatedShipWakeArray::PostReplicatedChange(
	const TArrayView<int32> ChangedIndices, const int32 FinalSize)
{
	if (!Owner) return;
	for (const int32 Index : ChangedIndices)
	{
		if (Items.IsValidIndex(Index)) Owner->ApplyReplicatedEvent(Items[Index].Event);
	}
}

ASWShipWakeReplicator::ASWShipWakeReplicator()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(30.0f);
	SetMinNetUpdateFrequency(10.0f);
	ReplicatedEvents.Owner = this;
}

void ASWShipWakeReplicator::BeginPlay()
{
	Super::BeginPlay();
	ReplicatedEvents.Owner = this;
	if (USWShipWakeSubsystem* State = GetWorld() ? GetWorld()->GetSubsystem<USWShipWakeSubsystem>() : nullptr)
	{
		State->RegisterReplicator(this);
		for (const FSWReplicatedShipWakeItem& Item : ReplicatedEvents.Items)
		{
			ApplyReplicatedEvent(Item.Event);
		}
	}
}

void ASWShipWakeReplicator::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		const USWShipWakeSubsystem* State = GetWorld()
			? GetWorld()->GetSubsystem<USWShipWakeSubsystem>() : nullptr;
		RemoveExpired(State ? State->GetServerTime() : GetWorld()->GetTimeSeconds());
	}
}

void ASWShipWakeReplicator::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASWShipWakeReplicator, ReplicatedEvents);
}

bool ASWShipWakeReplicator::AddServerEvent(const FSWShipWakeEvent& EventTemplate)
{
	if (!HasAuthority() || EventTemplate.InitialAmplitudeCm <= 0.0f)
	{
		return false;
	}
	USWShipWakeSubsystem* State = GetWorld()
		? GetWorld()->GetSubsystem<USWShipWakeSubsystem>() : nullptr;
	if (!State) return false;
	RemoveExpired(State->GetServerTime());
	const int32 MaxCapacity = USWShipWakeSubsystem::GetMaxCapacity();
	while (ReplicatedEvents.Items.Num() >= MaxCapacity)
	{
		int32 Oldest = 0;
		for (int32 Index = 1; Index < ReplicatedEvents.Items.Num(); ++Index)
		{
			if (ReplicatedEvents.Items[Index].Event.StartServerTime
				< ReplicatedEvents.Items[Oldest].Event.StartServerTime) Oldest = Index;
		}
		ReplicatedEvents.Items.RemoveAtSwap(Oldest, 1, EAllowShrinking::No);
		ReplicatedEvents.MarkArrayDirty();
	}

	FSWReplicatedShipWakeItem& Item = ReplicatedEvents.Items.AddDefaulted_GetRef();
	Item.Event = EventTemplate;
	Item.Event.EventId = NextEventId++;
	ReplicatedEvents.MarkItemDirty(Item);
	State->AddOrUpdateReplicatedEvent(Item.Event);
	ForceNetUpdate();
	return true;
}

void ASWShipWakeReplicator::ApplyReplicatedEvent(const FSWShipWakeEvent& Event) const
{
	if (USWShipWakeSubsystem* State = GetWorld() ? GetWorld()->GetSubsystem<USWShipWakeSubsystem>() : nullptr)
	{
		State->AddOrUpdateReplicatedEvent(Event);
	}
}

void ASWShipWakeReplicator::RemoveExpired(const double ServerTime)
{
	bool bChanged = false;
	for (int32 Index = ReplicatedEvents.Items.Num() - 1; Index >= 0; --Index)
	{
		if (ReplicatedEvents.Items[Index].Event.ExpireServerTime <= ServerTime)
		{
			ReplicatedEvents.Items.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			bChanged = true;
		}
	}
	if (bChanged)
	{
		ReplicatedEvents.MarkArrayDirty();
		ForceNetUpdate();
	}
}
