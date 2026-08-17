#include "DeckAI/DeckWaypointComponent.h"

#include "Algo/Unique.h"

UDeckWaypointComponent::UDeckWaypointComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
	SetMobility(EComponentMobility::Movable);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	InitSphereRadius(35.0f);
	SetHiddenInGame(true);
	bUseEditorCompositing = true;
	bDrawOnlyIfSelected = false;
	RefreshEditorVisualization();
}

void UDeckWaypointComponent::InitializeGeneratedWaypoint(
	int32 InWaypointId,
	int32 InGridX,
	int32 InGridY,
	bool bInCanSpawn,
	bool bInCanPatrol,
	bool bInCanUseInCombat)
{
	WaypointId = InWaypointId;
	GeneratedGridX = InGridX;
	GeneratedGridY = InGridY;
	bGeneratedFromDeckMesh = true;
	bCanSpawn = bInCanSpawn;
	bCanPatrol = bInCanPatrol;
	bCanUseInCombat = bInCanUseInCombat;
	RefreshEditorVisualization();
}

void UDeckWaypointComponent::SetGeneratedLinks(const TArray<int32>& InLinkedWaypointIds)
{
	LinkedWaypointIds = InLinkedWaypointIds;
	LinkedWaypointIds.Sort();
	LinkedWaypointIds.SetNum(Algo::Unique(LinkedWaypointIds));
}

void UDeckWaypointComponent::RefreshEditorVisualization()
{
	if (!bCanPatrol && !bCanUseInCombat)
	{
		ShapeColor = FColor(220, 45, 45);
	}
	else if (bCanSpawn)
	{
		ShapeColor = FColor(40, 200, 255);
	}
	else if (bCanUseInCombat)
	{
		ShapeColor = FColor(50, 220, 80);
	}
	else
	{
		ShapeColor = FColor(255, 190, 30);
	}
	MarkRenderStateDirty();
}

float UDeckWaypointComponent::GetRandomWaitTime(FRandomStream& RandomStream) const
{
	const float Minimum = FMath::Max(0.0f, MinWaitTime);
	const float Maximum = FMath::Max(Minimum, MaxWaitTime);
	return RandomStream.FRandRange(Minimum, Maximum);
}

#if WITH_EDITOR
void UDeckWaypointComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshEditorVisualization();
}
#endif
