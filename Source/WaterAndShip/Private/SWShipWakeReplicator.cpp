#include "SWShipWakeReplicator.h"
#include "Room/SWVoyageResetSubsystem.h"
#include "Room/SWVoyageSpawnLibrary.h"

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
	if (USWVoyageSpawnLibrary::IsVoyageGameplayBlocked(this)) return;
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
	DOREPLIFETIME(ASWShipWakeReplicator, VoyageGeneration);
}

bool ASWShipWakeReplicator::AddServerEvent(const FSWShipWakeEvent& EventTemplate)
{
	if (USWVoyageSpawnLibrary::IsVoyageGameplayBlocked(this)) return false;
	const USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	const int32 Generation = Voyage ? Voyage->GetGeneration() : 0;
	if (EventTemplate.Generation != 0 && EventTemplate.Generation != Generation) return false;
	ResetForVoyage(Generation);
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
	Item.Event.Generation = VoyageGeneration;
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

void ASWShipWakeReplicator::ResetForVoyage(int32 Generation)
{
	if (!HasAuthority() || Generation <= VoyageGeneration) return;
	VoyageGeneration = Generation;
	ReplicatedEvents.Items.Reset(); ReplicatedEvents.MarkArrayDirty();
	NextEventId = 1; ForceNetUpdate();
}

FName ASWShipWakeReplicator::GetVoyageParticipantId_Implementation() const
{
	USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	return Voyage ? Voyage->ResolveParticipantId(const_cast<ASWShipWakeReplicator*>(this)) : NAME_None;
}

ESWVoyageStepResult ASWShipWakeReplicator::PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	return ESWVoyageStepResult::Succeeded;
}

ESWVoyageStepResult ASWShipWakeReplicator::ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	// Authority clears at restore, after the generation is committed. Clients
	// retain FastArray state so replication arrival order cannot lose new events.
	return ESWVoyageStepResult::Succeeded;
}

ESWVoyageStepResult ASWShipWakeReplicator::RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	if (Context.bAuthority) ResetForVoyage(Context.Generation);
	return IsVoyageReady_Implementation(Context, OutError);
}

ESWVoyageStepResult ASWShipWakeReplicator::IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	const USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	if (!Voyage || !Voyage->IsCurrentGeneration(Context.Generation))
	{
		OutError = TEXT("ReplicatorVoyageGenerationInvalid");
		return ESWVoyageStepResult::Failed;
	}
	return VoyageGeneration == Context.Generation ? ESWVoyageStepResult::Succeeded : ESWVoyageStepResult::Pending;
}
