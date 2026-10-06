#include "DeckAI/DeckWaypointComponent.h"


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

void UDeckWaypointComponent::SetWaypointIdForAuthoring(int32 InWaypointId)
{
	WaypointId = FMath::Max(0, InWaypointId);
}

void UDeckWaypointComponent::RefreshEditorVisualization()
{
	ShapeColor = bCanSpawn ? FColor(40, 200, 255) : FColor(255, 190, 30);
	MarkRenderStateDirty();
}


#if WITH_EDITOR
void UDeckWaypointComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshEditorVisualization();
}
#endif
