#include "DeckAI/DeckWaypointComponent.h"

UDeckWaypointComponent::UDeckWaypointComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
	SetMobility(EComponentMobility::Movable);
}

float UDeckWaypointComponent::GetRandomWaitTime(FRandomStream& RandomStream) const
{
	const float Minimum = FMath::Max(0.0f, MinWaitTime);
	const float Maximum = FMath::Max(Minimum, MaxWaitTime);
	return RandomStream.FRandRange(Minimum, Maximum);
}
