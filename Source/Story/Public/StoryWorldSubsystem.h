#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Room/SWVoyageResetParticipant.h"
#include "StoryWorldSubsystem.generated.h"

class AStoryStateReplicator;

/** Creates exactly one transient story replicator in each gameplay world. */
UCLASS()
class STORY_API UStoryWorldSubsystem : public UWorldSubsystem, public ISWVoyageResetParticipant
{
	GENERATED_BODY()

public:
	virtual ESWVoyagePolicy GetVoyagePolicy_Implementation() const override { return ESWVoyagePolicy::Preserve; }
	virtual FName GetVoyageParticipantId_Implementation() const override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<AStoryStateReplicator> Replicator = nullptr;
};
