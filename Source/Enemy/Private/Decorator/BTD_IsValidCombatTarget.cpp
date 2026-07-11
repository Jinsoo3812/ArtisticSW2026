// Fill out your copyright notice in the Description page of Project Settings.

#include "Decorator/BTD_IsValidCombatTarget.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "Components/BaseHealthComponent.h"

UBTD_IsValidCombatTarget::UBTD_IsValidCombatTarget()
{
	NodeName = TEXT("Is Valid Combat Target");
	FlowAbortMode = EBTFlowAbortMode::Self;

	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTD_IsValidCombatTarget, BlackboardKey), AActor::StaticClass());
}

bool UBTD_IsValidCombatTarget::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	UBlackboardComponent* BlackboardComponent = OwnerComp.GetBlackboardComponent();
	if (!BlackboardComponent)
	{
		return false;
	}

	AActor* TargetActor = Cast<AActor>(BlackboardComponent->GetValueAsObject(GetSelectedBlackboardKey()));
	const bool bValidTarget = IsValidCombatTarget(TargetActor);

	if (!bValidTarget && bClearInvalidTarget)
	{
		BlackboardComponent->ClearValue(GetSelectedBlackboardKey());
	}

	return bValidTarget;
}

FString UBTD_IsValidCombatTarget::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("%s: %s must be a valid, alive combat target"),
		*Super::GetStaticDescription(),
		*BlackboardKey.SelectedKeyName.ToString());
}

bool UBTD_IsValidCombatTarget::IsValidCombatTarget(const AActor* TargetActor) const
{
	return IsValid(TargetActor)
		&& IsTargetAlive(TargetActor)
		&& IsTargetPoolActive(TargetActor);
}

bool UBTD_IsValidCombatTarget::IsTargetAlive(const AActor* TargetActor) const
{
	const UBaseHealthComponent* HealthComponent = TargetActor ? TargetActor->FindComponentByClass<UBaseHealthComponent>() : nullptr;
	if (!HealthComponent)
	{
		return !bRequireHealthComponent;
	}

	return !HealthComponent->IsDead();
}

bool UBTD_IsValidCombatTarget::IsTargetPoolActive(const AActor* TargetActor) const
{
	// Reserved for future enemy/object pooling state checks.
	return IsValid(TargetActor);
}
