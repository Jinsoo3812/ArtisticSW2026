#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "ShipAI/EnemyShipSkillModuleData.h"
#include "EnemyShipPatternRuntimeComponent.generated.h"

class UEnemyShipArchetypeData;
class UEnemyShipSkillModuleData;

USTRUCT(BlueprintType)
struct ENEMY_API FEnemyShipAbilitySelection
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Ship|Pattern")
	FGameplayTag AbilityTag;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Ship|Pattern")
	EEnemyShipSkillMovementPolicy MovementPolicy = EEnemyShipSkillMovementPolicy::ContinueNavigation;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Ship|Pattern")
	FName RuleId;

	bool IsValid() const { return AbilityTag.IsValid() && !RuleId.IsNone(); }
};

/** Per-ship mutable scheduler state for an immutable Archetype Data Asset. */
UCLASS(ClassGroup = (EnemyShip), meta = (BlueprintSpawnableComponent))
class ENEMY_API UEnemyShipPatternRuntimeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemyShipPatternRuntimeComponent();

	UFUNCTION(BlueprintCallable, Category = "Enemy Ship|Skills")
	void Configure(UEnemyShipArchetypeData* InArchetype);

	UFUNCTION(BlueprintCallable, Category = "Enemy Ship|Pattern")
	bool SelectAbility(AActor* TargetActor, FEnemyShipAbilitySelection& OutSelection);

	bool SelectAbilityAtTime(
		AActor* TargetActor,
		double CurrentTimeSeconds,
		FEnemyShipAbilitySelection& OutSelection);

	UFUNCTION(BlueprintCallable, Category = "Enemy Ship|Pattern")
	bool CommitSelection(const FEnemyShipAbilitySelection& Selection);

	UFUNCTION(BlueprintCallable, Category = "Enemy Ship|Pattern")
	void ResetRuntimeState(int32 RandomSeed = 0);

	int32 GetResolvedRuleCount() const { return SkillModules.Num(); }

private:
	bool IsModuleEligible(
		int32 ModuleIndex,
		AActor* TargetActor,
		const FGameplayTagContainer& OwnerTags) const;
	bool IsGrantedAbilityAvailable(const FGameplayTag& AbilityTag) const;
	int32 SelectEligibleIndex(const TArray<int32>& EligibleIndices);
	UPROPERTY(Transient)
	TObjectPtr<UEnemyShipArchetypeData> Archetype;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UEnemyShipSkillModuleData>> SkillModules;

	TSet<TObjectPtr<const UEnemyShipSkillModuleData>> ConsumedOneShotModules;
	FRandomStream RandomStream;
	int32 SequenceCursor = 0;
	TWeakObjectPtr<const UEnemyShipSkillModuleData> PendingModule;
};
