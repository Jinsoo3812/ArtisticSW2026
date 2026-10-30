#pragma once

#include "CoreMinimal.h"
#include "StoryConditionalSpawner.h"
#include "CampaignBossSpawner.generated.h"

class AChestSpawnPoint;

/** Ground campaign boss paired with its guarded, guaranteed-reward chest. */
UCLASS()
class ENEMY_API ACampaignBossSpawner : public AStoryConditionalSpawner
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual bool FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError) override;

	UPROPERTY(EditInstanceOnly, Category = "Campaign")
	TObjectPtr<AChestSpawnPoint> RewardChest;

private:
	UFUNCTION()
	void ConfigureSpawnedBoss(AActor* Actor);
	UFUNCTION()
	void RefreshRewardReservation();
};
