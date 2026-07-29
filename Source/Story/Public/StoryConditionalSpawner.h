#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StoryFacadeSubsystem.h"
#include "StoryConditionalSpawner.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStoryConditionalActorSpawned, AActor*, SpawnedActor);

/**
 * Server-only placement controlled by two readable story nodes.
 *
 * Example for boss 2:
 * RequiredStoryNode = SupplyPatrolQuestAccepted
 * StopAfterStoryNode = MiddleBoss2Defeated
 */
UCLASS()
class STORY_API AStoryConditionalSpawner : public AActor
{
	GENERATED_BODY()

public:
	AStoryConditionalSpawner();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Story")
	void RefreshFromStory();

	UFUNCTION(BlueprintPure, Category = "Story")
	AActor* GetSpawnedActor() const { return SpawnedActor.Get(); }

	UPROPERTY(BlueprintAssignable, Category = "Story")
	FOnStoryConditionalActorSpawned OnActorSpawned;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Story")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	TSoftClassPtr<AActor> SpawnedActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	EStoryNode RequiredStoryNode = EStoryNode::ReconQuestAccepted;

	/** Enable for one-time actors such as bosses. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	bool bStopAfterStoryNode = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story",
		meta = (EditCondition = "bStopAfterStoryNode"))
	EStoryNode StopAfterStoryNode = EStoryNode::MiddleBoss1Defeated;

private:
	UFUNCTION()
	void HandleStoryChanged();

	UFUNCTION()
	void HandleSpawnedActorDestroyed(AActor* DestroyedActor);

	TWeakObjectPtr<AActor> SpawnedActor;
};
