#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "EnemyShipPatternRuntimeComponent.generated.h"

class UEnemyShipPatternData;

USTRUCT(BlueprintType)
struct ENEMY_API FEnemyShipAbilitySelection
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Ship|Pattern")
	FGameplayTag AbilityTag;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Ship|Pattern")
	EEnemyShipSkillMovementPolicy MovementPolicy = EEnemyShipSkillMovementPolicy::ContinueNavigation;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Ship|Pattern")
	int32 RuleIndex = INDEX_NONE;

	bool IsValid() const { return AbilityTag.IsValid() && RuleIndex != INDEX_NONE; }
};

/** Per-ship mutable scheduler state for an immutable Pattern Data Asset. */
UCLASS(ClassGroup = (EnemyShip), meta = (BlueprintSpawnableComponent))
class ENEMY_API UEnemyShipPatternRuntimeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemyShipPatternRuntimeComponent();

	UFUNCTION(BlueprintCallable, Category = "Enemy Ship|Pattern")
	void SetPattern(UEnemyShipPatternData* InPattern);

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Pattern")
	UEnemyShipPatternData* GetPattern() const { return Pattern; }

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

	double GetLastCommittedTime(int32 RuleIndex) const;

private:
	bool IsRuleEligible(
		int32 RuleIndex,
		AActor* TargetActor,
		double CurrentTimeSeconds,
		float OwnerHealthRatio,
		const FGameplayTagContainer& OwnerTags) const;
	bool IsGrantedAbilityAvailable(const FGameplayTag& AbilityTag) const;
	int32 SelectEligibleIndex(const TArray<int32>& EligibleIndices);

	UPROPERTY(Transient)
	TObjectPtr<UEnemyShipPatternData> Pattern;

	TMap<int32, double> LastCommittedTimes;
	TSet<int32> ConsumedOneShotRules;
	FRandomStream RandomStream;
	int32 SequenceCursor = 0;
	double PendingSelectionTime = 0.0;
	int32 PendingRuleIndex = INDEX_NONE;
};
