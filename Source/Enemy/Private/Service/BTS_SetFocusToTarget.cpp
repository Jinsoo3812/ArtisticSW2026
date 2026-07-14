// Fill out your copyright notice in the Description page of Project Settings.

#include "Service/BTS_SetFocusToTarget.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "Components/BaseHealthComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UBTS_SetFocusToTarget::UBTS_SetFocusToTarget()
{
	NodeName = TEXT("Set Focus To Target");
	Interval = 0.1f;
	RandomDeviation = 0.02f;

	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTS_SetFocusToTarget, BlackboardKey), AActor::StaticClass());
}

void UBTS_SetFocusToTarget::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const
{
	InitializeNodeMemory<FEnemyFocusServiceMemory>(NodeMemory, InitType);
}

void UBTS_SetFocusToTarget::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const
{
	CleanupNodeMemory<FEnemyFocusServiceMemory>(NodeMemory, CleanupType);
}

void UBTS_SetFocusToTarget::OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::OnBecomeRelevant(OwnerComp, NodeMemory);

	FEnemyFocusServiceMemory* Memory = CastInstanceNodeMemory<FEnemyFocusServiceMemory>(NodeMemory);
	if (!Memory)
	{
		return;
	}

	Memory->Reset();
	ApplyFocusFacingMode(OwnerComp, *Memory);
	TrySetFocus(OwnerComp, *Memory);
}

void UBTS_SetFocusToTarget::OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FEnemyFocusServiceMemory* Memory = CastInstanceNodeMemory<FEnemyFocusServiceMemory>(NodeMemory);
	if (Memory)
	{
		ClearOwnedFocus(OwnerComp, *Memory);
		RestoreMovementRotationMode(OwnerComp, *Memory);
	}

	Super::OnCeaseRelevant(OwnerComp, NodeMemory);
}

void UBTS_SetFocusToTarget::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	FEnemyFocusServiceMemory* Memory = CastInstanceNodeMemory<FEnemyFocusServiceMemory>(NodeMemory);
	if (!Memory)
	{
		return;
	}

	TrySetFocus(OwnerComp, *Memory);
}

FString UBTS_SetFocusToTarget::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("Set %s focus to blackboard target: %s"),
		FocusPriority == EAIFocusPriority::Gameplay ? TEXT("Gameplay") : TEXT("AI"),
		*BlackboardKey.SelectedKeyName.ToString());
}

bool UBTS_SetFocusToTarget::TrySetFocus(UBehaviorTreeComponent& OwnerComp, FEnemyFocusServiceMemory& Memory) const
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	UBlackboardComponent* BlackboardComponent = OwnerComp.GetBlackboardComponent();
	if (!AIController || !BlackboardComponent)
	{
		Memory.Reset();
		return false;
	}

	AActor* TargetActor = Cast<AActor>(BlackboardComponent->GetValueAsObject(GetSelectedBlackboardKey()));
	if (!IsValidFocusTarget(TargetActor))
	{
		ClearOwnedFocus(OwnerComp, Memory);

		if (bClearInvalidTarget)
		{
			BlackboardComponent->ClearValue(GetSelectedBlackboardKey());
		}

		return false;
	}

	if (Memory.FocusActorSet.Get() != TargetActor)
	{
		ClearOwnedFocus(OwnerComp, Memory);
		AIController->SetFocus(TargetActor, FocusPriority);
		Memory.FocusActorSet = TargetActor;
	}

	return true;
}

void UBTS_SetFocusToTarget::ClearOwnedFocus(UBehaviorTreeComponent& OwnerComp, FEnemyFocusServiceMemory& Memory) const
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController)
	{
		Memory.FocusActorSet.Reset();
		return;
	}

	AActor* FocusActor = AIController->GetFocusActorForPriority(FocusPriority);
	if (Memory.FocusActorSet.IsValid() && FocusActor == Memory.FocusActorSet.Get())
	{
		AIController->ClearFocus(FocusPriority);
	}

	Memory.FocusActorSet.Reset();
}

void UBTS_SetFocusToTarget::ApplyFocusFacingMode(UBehaviorTreeComponent& OwnerComp, FEnemyFocusServiceMemory& Memory) const
{
	if (!bUseControllerDesiredRotationWhileFocused || Memory.bSavedMovementRotationMode)
	{
		return;
	}

	const AAIController* AIController = OwnerComp.GetAIOwner();
	ACharacter* Character = AIController ? Cast<ACharacter>(AIController->GetPawn()) : nullptr;
	UCharacterMovementComponent* MovementComponent = Character ? Character->GetCharacterMovement() : nullptr;
	if (!MovementComponent)
	{
		return;
	}

	Memory.bSavedMovementRotationMode = true;
	Memory.bPreviousOrientRotationToMovement = MovementComponent->bOrientRotationToMovement;
	Memory.bPreviousUseControllerDesiredRotation = MovementComponent->bUseControllerDesiredRotation;

	MovementComponent->bOrientRotationToMovement = false;
	MovementComponent->bUseControllerDesiredRotation = true;
}

void UBTS_SetFocusToTarget::RestoreMovementRotationMode(UBehaviorTreeComponent& OwnerComp, FEnemyFocusServiceMemory& Memory) const
{
	if (!Memory.bSavedMovementRotationMode)
	{
		return;
	}

	const AAIController* AIController = OwnerComp.GetAIOwner();
	ACharacter* Character = AIController ? Cast<ACharacter>(AIController->GetPawn()) : nullptr;
	UCharacterMovementComponent* MovementComponent = Character ? Character->GetCharacterMovement() : nullptr;
	if (MovementComponent)
	{
		MovementComponent->bOrientRotationToMovement = Memory.bPreviousOrientRotationToMovement;
		MovementComponent->bUseControllerDesiredRotation = Memory.bPreviousUseControllerDesiredRotation;
	}

	Memory.bSavedMovementRotationMode = false;
}

bool UBTS_SetFocusToTarget::IsValidFocusTarget(const AActor* TargetActor) const
{
	return IsValid(TargetActor)
		&& IsTargetAlive(TargetActor)
		&& IsTargetPoolActive(TargetActor);
}

bool UBTS_SetFocusToTarget::IsTargetAlive(const AActor* TargetActor) const
{
	const UBaseHealthComponent* HealthComponent = TargetActor ? TargetActor->FindComponentByClass<UBaseHealthComponent>() : nullptr;
	if (!HealthComponent)
	{
		return !bRequireHealthComponent;
	}

	return !HealthComponent->IsDead();
}

bool UBTS_SetFocusToTarget::IsTargetPoolActive(const AActor* TargetActor) const
{
	// Reserved for future object-pooling state checks.
	return IsValid(TargetActor);
}
