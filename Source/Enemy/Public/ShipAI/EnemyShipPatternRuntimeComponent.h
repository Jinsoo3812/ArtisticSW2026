#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "ShipAI/EnemyShipPatternData.h"
#include "EnemyShipPatternRuntimeComponent.generated.h"

class UEnemyShipPatternData;
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

/** Per-ship mutable scheduler state for an immutable Pattern Data Asset. */
UCLASS(ClassGroup = (EnemyShip), meta = (BlueprintSpawnableComponent))
class ENEMY_API UEnemyShipPatternRuntimeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemyShipPatternRuntimeComponent();

	UFUNCTION(BlueprintCallable, Category = "Enemy Ship|Pattern")
	void SetPattern(UEnemyShipPatternData* InPattern);

	UFUNCTION(BlueprintCallable, Category = "Enemy Ship|Pattern")
	void SetCoreSkillModules(const TArray<UEnemyShipSkillModuleData*>& InCoreModules);

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

	double GetLastCommittedTime(FName RuleId) const;
	int32 GetResolvedRuleCount() const { return ResolvedRules.Num(); }

private:
	bool IsRuleEligible(
		int32 RuleIndex,
		AActor* TargetActor,
		double CurrentTimeSeconds,
		float OwnerHealthRatio,
		const FGameplayTagContainer& OwnerTags) const;
	bool IsGrantedAbilityAvailable(const FGameplayTag& AbilityTag) const;
	int32 SelectEligibleIndex(const TArray<int32>& EligibleIndices);
	void RebuildResolvedRules();

	UPROPERTY(Transient)
	TObjectPtr<UEnemyShipPatternData> Pattern;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UEnemyShipSkillModuleData>> CoreSkillModules;

	TArray<FEnemyShipSkillRule> ResolvedRules;

	TMap<FName, double> LastCommittedTimes;
	TSet<FName> ConsumedOneShotRules;
	FRandomStream RandomStream;
	int32 SequenceCursor = 0;
	double PendingSelectionTime = 0.0;
	FName PendingRuleId;
};
