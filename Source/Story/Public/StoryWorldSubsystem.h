#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "StoryWorldSubsystem.generated.h"

class AStoryStateReplicator;

/** Creates exactly one transient story replicator in each gameplay world. */
UCLASS()
class STORY_API UStoryWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<AStoryStateReplicator> Replicator = nullptr;
};
