#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SWBuoyancyDebugSubsystem.generated.h"

/** Draws every non-ship SW buoyancy owner; AShip keeps its richer network-physics overlay. */
UCLASS()
class WATERANDSHIP_API USWBuoyancyDebugSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
};
