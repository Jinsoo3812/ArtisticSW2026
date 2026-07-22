// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BaseGameplayTags.h"

#include "BTT_EnemyBasicAttack.generated.h"

class UAbilitySystemComponent;
class UBehaviorTreeComponent;

UCLASS()
class ENEMY_API UBTT_EnemyBasicAttack : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_EnemyBasicAttack();

	UPROPERTY(EditAnywhere, Category = "Attack")
	FGameplayTag AttackAbilityAssetTag;

	UPROPERTY(EditAnywhere, Category = "Attack")
	FGameplayTag AttackStateTag;

public:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	// virtual FString GetStaticDescription() const override;

protected:
	bool ActivateCurrentWeaponAbilityByAssetTag(class ABaseEnemy* Enemy, const FGameplayTag& AbilityAssetTag) const;
	bool IsAbilityClassTagged(TSubclassOf<class UGameplayAbility> AbilityClass, const FGameplayTag& AbilityAssetTag) const;
	void CleanupTagDelegate();
	void FinishAttackTask(EBTNodeResult::Type Result);
	void OnAttackTagChanged(const FGameplayTag CallbackTag, int32 NewCount);
	
private:
	TWeakObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;
	TWeakObjectPtr<UAbilitySystemComponent> CachedASC;
	FDelegateHandle AttackTagDelegateHandle;
	bool bObservedAttackStart = false;
	bool bRecovering = false;
	float RecoveryProgress = 0.0f;
	float BaseAttackCooldown = 0.0f;
	
};
