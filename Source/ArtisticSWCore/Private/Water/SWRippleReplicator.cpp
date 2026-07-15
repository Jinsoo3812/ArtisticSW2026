#include "Water/SWRippleReplicator.h"

#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Water/SWRippleStateSubsystem.h"

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
				StateSubsystem->AddOrUpdateReplicatedEvent(Item.Event);
			}
		}
	}
}

void ASWRippleReplicator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
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
}

bool ASWRippleReplicator::AddServerRipple(
	const FVector2D& Origin,
	float InitialAmplitude,
	float WaveSpeed,
	float DecayRate,
	float WaveLength)
{
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
	if (ReplicatedRipples.Items.Num() >= MaxReplicatedRipples)
	{
		int32 OldestIndex = 0;
		for (int32 Index = 1; Index < ReplicatedRipples.Items.Num(); ++Index)
		{
			if (ReplicatedRipples.Items[Index].Event.ExpireServerTime
				< ReplicatedRipples.Items[OldestIndex].Event.ExpireServerTime)
			{
				OldestIndex = Index;
			}
		}
		ReplicatedRipples.Items.RemoveAtSwap(OldestIndex, 1, EAllowShrinking::No);
		ReplicatedRipples.MarkArrayDirty();
	}

	FSWReplicatedRippleItem& NewItem = ReplicatedRipples.Items.AddDefaulted_GetRef();
	NewItem.Event.EventId = NextEventId++;
	NewItem.Event.Origin = Origin;
	NewItem.Event.StartServerTime = ServerTime;
	NewItem.Event.InitialAmplitude = InitialAmplitude;
	NewItem.Event.WaveSpeed = WaveSpeed;
	NewItem.Event.DecayRate = DecayRate;
	NewItem.Event.WaveLength = WaveLength;
	NewItem.Event.ExpireServerTime = ServerTime + static_cast<double>(Lifetime);
	ReplicatedRipples.MarkItemDirty(NewItem);

	StateSubsystem->AddOrUpdateReplicatedEvent(NewItem.Event);
	ForceNetUpdate();
	return true;
}

void ASWRippleReplicator::ApplyReplicatedEvent(const FSWRippleEvent& Event) const
{
	if (UWorld* World = GetWorld())
	{
		if (USWRippleStateSubsystem* StateSubsystem = World->GetSubsystem<USWRippleStateSubsystem>())
		{
			StateSubsystem->AddOrUpdateReplicatedEvent(Event);
		}
	}
}

void ASWRippleReplicator::RemoveExpiredActiveEvents(double ServerTime)
{
	bool bRemoved = false;
	for (int32 Index = ReplicatedRipples.Items.Num() - 1; Index >= 0; --Index)
	{
		if (ReplicatedRipples.Items[Index].Event.ExpireServerTime <= ServerTime)
		{
			ReplicatedRipples.Items.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			bRemoved = true;
		}
	}

	if (bRemoved)
	{
		ReplicatedRipples.MarkArrayDirty();
		ForceNetUpdate();
	}
}
