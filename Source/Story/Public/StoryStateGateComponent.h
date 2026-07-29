#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StoryFacadeSubsystem.h"
#include "StoryStateGateComponent.generated.h"

/**
 * Reversible shared-world presentation gate.
 *
 * Use this for an already placed NPC, door, clue, or facility. Use
 * AStoryConditionalSpawner when an actor must not exist before its story state.
 */
UCLASS(ClassGroup = (Story), meta = (BlueprintSpawnableComponent))
class STORY_API UStoryStateGateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStoryStateGateComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Story")
	void RefreshFromStory();

	UFUNCTION(BlueprintPure, Category = "Story")
	bool IsGateOpen() const { return bGateOpen; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	EStoryNode RequiredStoryNode = EStoryNode::ReconQuestAccepted;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	bool bHideActorWhenClosed = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	bool bDisableCollisionWhenClosed = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	bool bDisableActorTickWhenClosed = true;

private:
	UFUNCTION()
	void HandleStoryChanged();

	bool bGateOpen = false;
	bool bInitialHidden = false;
	bool bInitialCollisionEnabled = true;
	bool bInitialTickEnabled = true;
};
