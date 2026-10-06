#include "Water/SWRippleReplicator.h"

#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Water/SWRippleProfile.h"
#include "Water/SWRippleSettings.h"
#include "Water/SWRippleStateSubsystem.h"
#include "Room/SWVoyageResetSubsystem.h"
#include "Room/SWVoyageSpawnLibrary.h"

namespace
{
	void LogRippleNetworkCheck(const FSWRippleEvent& Event, const TCHAR* Role)
	{
		if (!FParse::Param(FCommandLine::Get(), TEXT("RippleQueryDiagnostics")))
		{
			return;
		}

		const FVector2D QueryPosition = Event.Origin + FVector2D(37.0f, 19.0f);
		const double QueryServerTime = Event.StartServerTime + 0.375;
		const float Height = FSWRippleEvaluator::EvaluateHeight(
			QueryPosition,
			QueryServerTime,
			MakeArrayView(&Event, 1));
		UE_LOG(LogTemp, Warning,
			TEXT("[RIPPLE-NET-CHECK] Role=%s EventId=%d Position=%s ServerTime=%.9f Height=%.9f"),
			Role,
			Event.EventId,
			*QueryPosition.ToString(),
			QueryServerTime,
			Height);
	}
}

void FSWReplicatedRippleArray::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	if (!Owner)
	{
		return;
	}

	for (const int32 Index : AddedIndices)
	{
		if (Items.IsValidIndex(Index))
		{
			Owner->ApplyReplicatedEvent(Items[Index].Event);
		}
	}
}

void FSWReplicatedRippleArray::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize)
{
	if (!Owner)
	{
		return;
	}

	for (const int32 Index : ChangedIndices)
	{
		if (Items.IsValidIndex(Index))
		{
			Owner->ApplyReplicatedEvent(Items[Index].Event);
		}
	}
}

ASWRippleReplicator::ASWRippleReplicator()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(30.0f);
	SetMinNetUpdateFrequency(10.0f);
	ReplicatedRipples.Owner = this;
}

void ASWRippleReplicator::BeginPlay()
{
	Super::BeginPlay();
	ReplicatedRipples.Owner = this;

	if (UWorld* World = GetWorld())
	{
		if (USWRippleStateSubsystem* StateSubsystem = World->GetSubsystem<USWRippleStateSubsystem>())
		{
			StateSubsystem->RegisterReplicator(this);
			for (const FSWReplicatedRippleItem& Item : ReplicatedRipples.Items)
			{
				ApplyReplicatedEvent(Item.Event);
			}
		}
	}
}

void ASWRippleReplicator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (USWVoyageSpawnLibrary::IsVoyageGameplayBlocked(this)) return;
	if (HasAuthority())
	{
		const USWRippleStateSubsystem* StateSubsystem = GetWorld()
			? GetWorld()->GetSubsystem<USWRippleStateSubsystem>()
			: nullptr;
		RemoveExpiredActiveEvents(StateSubsystem ? StateSubsystem->GetServerTime() : GetWorld()->GetTimeSeconds());
	}
}

void ASWRippleReplicator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASWRippleReplicator, ReplicatedRipples);
	DOREPLIFETIME(ASWRippleReplicator, VoyageGeneration);
}

bool ASWRippleReplicator::AddServerRipple(
	const FVector2D& Origin,
	float InitialAmplitude,
	float WaveSpeed,
	float DecayRate,
	float WaveLength)
{
	if (USWVoyageSpawnLibrary::IsVoyageGameplayBlocked(this)) return false;
	const USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	ResetForVoyage(Voyage ? Voyage->GetGeneration() : 0);
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_Ripple_AddServerRipple);
	if (!HasAuthority() || InitialAmplitude <= 0.0f || WaveLength <= UE_SMALL_NUMBER)
	{
		return false;
	}

	USWRippleStateSubsystem* StateSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<USWRippleStateSubsystem>()
		: nullptr;
	if (!StateSubsystem)
	{
		return false;
	}

	const double ServerTime = StateSubsystem->GetServerTime();
	const float EffectiveDecayRate = FMath::Max(DecayRate, 0.01f);
	const float Lifetime = FMath::Clamp(
		FMath::Loge(FMath::Max(InitialAmplitude, 0.01f) / 0.01f) / EffectiveDecayRate,
		0.5f,
		10.0f);

	RemoveExpiredActiveEvents(ServerTime);
	const int32 MaxRippleCount = GetDefault<USWRippleSettings>()->GetMaxRippleCount();
	while (ReplicatedRipples.Items.Num() >= MaxRippleCount)
	{
		int32 OldestIndex = 0;
		for (int32 Index = 1; Index < ReplicatedRipples.Items.Num(); ++Index)
		{
			if (ReplicatedRipples.Items[Index].Event.EventId
				< ReplicatedRipples.Items[OldestIndex].Event.EventId)
			{
				OldestIndex = Index;
			}
		}
		ReplicatedRipples.Items.RemoveAtSwap(OldestIndex, 1, EAllowShrinking::No);
		FSWRippleProfile::RecordReplicatedEventsRemoved(1);
		ReplicatedRipples.MarkArrayDirty();
	}

	FSWReplicatedRippleItem& NewItem = ReplicatedRipples.Items.AddDefaulted_GetRef();
	NewItem.Event.EventId = NextEventId++;
	NewItem.Event.Generation = VoyageGeneration;
	NewItem.Event.Origin = Origin;
	NewItem.Event.StartServerTime = ServerTime;
	NewItem.Event.InitialAmplitude = InitialAmplitude;
	NewItem.Event.WaveSpeed = WaveSpeed;
	NewItem.Event.DecayRate = DecayRate;
	NewItem.Event.WaveLength = WaveLength;
	NewItem.Event.ExpireServerTime = ServerTime + static_cast<double>(Lifetime);
	ReplicatedRipples.MarkItemDirty(NewItem);

	StateSubsystem->AddOrUpdateReplicatedEvent(NewItem.Event);
	FSWRippleProfile::RecordAuthoritativeEventAdded();
	LogRippleNetworkCheck(NewItem.Event, TEXT("Authority"));
	if (FParse::Param(FCommandLine::Get(), TEXT("RippleDiagnostics")))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RIPPLE-LATENCY][Authority] EventCreated Id=%d Start=%.6f Origin=%s"),
			NewItem.Event.EventId,
			NewItem.Event.StartServerTime,
			*NewItem.Event.Origin.ToString());
	}
	ForceNetUpdate();
	return true;
}

void ASWRippleReplicator::ApplyReplicatedEvent(const FSWRippleEvent& Event) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_Ripple_ApplyReplicatedEvent);
	if (UWorld* World = GetWorld())
	{
		if (USWRippleStateSubsystem* StateSubsystem = World->GetSubsystem<USWRippleStateSubsystem>())
		{
			StateSubsystem->AddOrUpdateReplicatedEvent(Event);
			FSWRippleProfile::RecordReplicatedEventApplied();
			LogRippleNetworkCheck(Event, TEXT("Client"));
		}
	}
}

void ASWRippleReplicator::RemoveExpiredActiveEvents(double ServerTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_Ripple_RemoveExpiredReplicatedEvents);
	bool bRemoved = false;
	int32 RemovedCount = 0;
	for (int32 Index = ReplicatedRipples.Items.Num() - 1; Index >= 0; --Index)
	{
		if (ReplicatedRipples.Items[Index].Event.ExpireServerTime <= ServerTime)
		{
			ReplicatedRipples.Items.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			bRemoved = true;
			++RemovedCount;
		}
	}

	if (bRemoved)
	{
		FSWRippleProfile::RecordReplicatedEventsRemoved(RemovedCount);
		ReplicatedRipples.MarkArrayDirty();
		ForceNetUpdate();
	}
}

void ASWRippleReplicator::ResetForVoyage(int32 Generation)
{
	if (!HasAuthority() || Generation < VoyageGeneration || Generation == VoyageGeneration) return;
	VoyageGeneration = Generation;
	ReplicatedRipples.Items.Reset(); ReplicatedRipples.MarkArrayDirty();
	NextEventId = 1; ForceNetUpdate();
}

FName ASWRippleReplicator::GetVoyageParticipantId_Implementation() const
{
	USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	return Voyage ? Voyage->ResolveParticipantId(const_cast<ASWRippleReplicator*>(this)) : NAME_None;
}

ESWVoyageStepResult ASWRippleReplicator::PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	return ESWVoyageStepResult::Succeeded;
}

ESWVoyageStepResult ASWRippleReplicator::ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	// Authority clears at restore, after the generation is committed. Clients
	// retain FastArray state so replication arrival order cannot lose new events.
	return ESWVoyageStepResult::Succeeded;
}

ESWVoyageStepResult ASWRippleReplicator::RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	if (Context.bAuthority) ResetForVoyage(Context.Generation);
	return IsVoyageReady_Implementation(Context, OutError);
}

ESWVoyageStepResult ASWRippleReplicator::IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	const USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	if (!Voyage || !Voyage->IsCurrentGeneration(Context.Generation))
	{
		OutError = TEXT("ReplicatorVoyageGenerationInvalid");
		return ESWVoyageStepResult::Failed;
	}
	return VoyageGeneration == Context.Generation ? ESWVoyageStepResult::Succeeded : ESWVoyageStepResult::Pending;
}
