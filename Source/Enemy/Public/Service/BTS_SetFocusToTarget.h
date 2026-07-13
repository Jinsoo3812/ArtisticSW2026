// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "BehaviorTree/Services/BTService_BlackboardBase.h"
#include "BTS_SetFocusToTarget.generated.h"

class UBaseHealthComponent;

struct FEnemyFocusServiceMemory
{
	TWeakObjectPtr<AActor> FocusActorSet;
	uint8 bSavedMovementRotationMode : 1;
	uint8 bPreviousOrientRotationToMovement : 1;
	uint8 bPreviousUseControllerDesiredRotation : 1;

	void Reset()
	{
		FocusActorSet.Reset();
		bSavedMovementRotationMode = false;
		bPreviousOrientRotationToMovement = false;
		bPreviousUseControllerDesiredRotation = false;
	}
};

/**
 * Keeps AI focus on a selected blackboard target while the owning BT branch is active.
 *
 * The service owns facing only. Movement tasks such as strafe/attack should not
 * implement their own look-at logic.
 */
UCLASS()
class ENEMY_API UBTS_SetFocusToTarget : public UBTService_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTS_SetFocusToTarget();

protected:
	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FEnemyFocusServiceMemory); }
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const override;
	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	// AI의 이전 행동을 기억해서 복구, Service가 설정했던 Focus만 Clear
	virtual void OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	
	// Target이 바뀌거나 죽었을 때 Focus를 Clear하고 새 Target으로 Focus를 설정
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual FString GetStaticDescription() const override;

	bool TrySetFocus(UBehaviorTreeComponent& OwnerComp, FEnemyFocusServiceMemory& Memory) const;
	void ClearOwnedFocus(UBehaviorTreeComponent& OwnerComp, FEnemyFocusServiceMemory& Memory) const;
	void ApplyFocusFacingMode(UBehaviorTreeComponent& OwnerComp, FEnemyFocusServiceMemory& Memory) const;
	void RestoreMovementRotationMode(UBehaviorTreeComponent& OwnerComp, FEnemyFocusServiceMemory& Memory) const;
	bool IsValidFocusTarget(const AActor* TargetActor) const;
	bool IsTargetAlive(const AActor* TargetActor) const;
	bool IsTargetPoolActive(const AActor* TargetActor) const;

protected:
	UPROPERTY()
	uint8 FocusPriority = EAIFocusPriority::Gameplay;

	UPROPERTY(EditAnywhere, Category = "Focus")
	bool bClearInvalidTarget = true;

	UPROPERTY(EditAnywhere, Category = "Focus")
	bool bRequireHealthComponent = false;

	UPROPERTY(EditAnywhere, Category = "Facing")
	bool bUseControllerDesiredRotationWhileFocused = true;
};
