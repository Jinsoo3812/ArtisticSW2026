#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Room/SWVoyageResetParticipant.h"
#include "SWBuoyancyDebugSubsystem.generated.h"

/** Draws every non-ship SW buoyancy owner; AShip keeps its richer network-physics overlay. */
UCLASS()
class WATERANDSHIP_API USWBuoyancyDebugSubsystem : public UTickableWorldSubsystem, public ISWVoyageResetParticipant
{
	GENERATED_BODY()

public:
	virtual ESWVoyagePolicy GetVoyagePolicy_Implementation() const override;
	virtual FName GetVoyageParticipantId_Implementation() const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
};
