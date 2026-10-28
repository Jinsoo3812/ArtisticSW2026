#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "ChestGroundGuardBindingComponent.generated.h"

class ABaseEnemy;
class AGroundEnemySpawner;

/** Chest-owned adapter for the ground spawner's existing spawn notifications. */
UCLASS(ClassGroup = (Chest), meta = (BlueprintSpawnableComponent))
class ENEMY_API UChestGroundGuardBindingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UChestGroundGuardBindingComponent();
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Chest|Guards")
	TObjectPtr<AGroundEnemySpawner> GuardSpawner;

private:
	UFUNCTION()
	void HandleEnemySpawned(ABaseEnemy* Enemy, FGameplayTag EnemyTypeTag);
};
