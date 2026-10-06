#include "DeckAI/DeckWalkHeightResolver.h"

#include "DeckAI/DeckWaypointComponent.h"
#include "ShipAI/EnemyShip.h"

bool FDeckWalkHeightResolver::Resolve(const FDeckWalkSurfaceSettings& Surface,
	const AEnemyShip& Ship, const FTransform& Frame, FDeckWalkHeightBand& OutBand, FString& OutError)
{
	OutBand = FDeckWalkHeightBand();
	OutError.Reset();
	if (Surface.HeightMode == EDeckWalkHeightMode::LocalRange)
	{
		OutBand.MinimumZ = Surface.MinimumFloorZ;
		OutBand.MaximumZ = Surface.MaximumFloorZ;
	}
	else if (Surface.HeightMode == EDeckWalkHeightMode::WaypointReference)
	{
		const UDeckWaypointComponent* Reference = Ship.GetDeckWaypoint(Surface.HeightReferencePointId);
		if (!IsValid(Reference))
		{
			OutError = FString::Printf(TEXT("Missing height reference WaypointId=%d"), Surface.HeightReferencePointId);
			return false;
		}
		if (!FMath::IsFinite(Surface.HeightBelowReference) || !FMath::IsFinite(Surface.HeightAboveReference)
			|| Surface.HeightBelowReference < 0.0f || Surface.HeightAboveReference < 0.0f)
		{
			OutError = TEXT("Height reference offsets must be finite and non-negative");
			return false;
		}
		// Reading RelativeLocation.Z would be wrong for points attached under a transformed parent.
		OutBand.ReferenceLocalPosition = Frame.InverseTransformPosition(Reference->GetComponentLocation());
		if (OutBand.ReferenceLocalPosition.ContainsNaN())
		{
			OutError = TEXT("Invalid height reference transform");
			return false;
		}
		OutBand.bUseReferenceHeight = true;
		OutBand.MinimumZ = OutBand.ReferenceLocalPosition.Z - Surface.HeightBelowReference;
		OutBand.MaximumZ = OutBand.ReferenceLocalPosition.Z + Surface.HeightAboveReference;
	}
	else
	{
		OutError = TEXT("Unknown height reference mode");
		return false;
	}
	if (!FMath::IsFinite(OutBand.MinimumZ) || !FMath::IsFinite(OutBand.MaximumZ)
		|| OutBand.MaximumZ - OutBand.MinimumZ <= KINDA_SMALL_NUMBER)
	{
		OutError = TEXT("Invalid or empty sampling height band");
		return false;
	}
	return true;
}
