#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StoryTypes.h"
#include "StoryStateReplicator.generated.h"

/**
 * Per-world network mirror for the GameInstance story subsystem.
 *
 * The world subsystem creates one automatically. It has no progression logic.
 */
UCLASS(NotBlueprintable, Transient)
class STORY_API AStoryStateReplicator : public AActor
{
	GENERATED_BODY()

public:
	AStoryStateReplicator();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SetSnapshot(const FStoryProgressSnapshot& NewSnapshot);
	const FStoryProgressSnapshot& GetSnapshot() const { return Snapshot; }

private:
	UFUNCTION()
	void OnRep_Snapshot();

	void ApplySnapshotToSubsystem();

	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FStoryProgressSnapshot Snapshot;
};
