#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWWaterFoamRuntimeProbe.generated.h"

/**
 * Runtime-only diagnostic probe for isolated V5/V6 realistic-water maps.
 *
 * It does not drive rendering. It only reports which foam systems exist at
 * runtime and whether the CPU/GPU persistent-foam handoff has a render target.
 */
UCLASS(BlueprintType)
class WATERANDSHIP_API ASWWaterFoamRuntimeProbe : public AActor
{
	GENERATED_BODY()

public:
	ASWWaterFoamRuntimeProbe();

protected:
	virtual void BeginPlay() override;

private:
	void LogRuntimeState(const FString& Phase) const;
};

