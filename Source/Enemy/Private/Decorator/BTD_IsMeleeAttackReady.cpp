#include "Decorator/BTD_IsMeleeAttackReady.h"

#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "BaseEnemy.h"
#include "BaseGameplayTags.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Weapon/BaseWeaponComponent.h"

UBTD_IsMeleeAttackReady::UBTD_IsMeleeAttackReady()
{
	NodeName = TEXT("Is Melee Attack Ready");
	bCreateNodeInstance = true;
	FlowAbortMode = EBTFlowAbortMode::LowerPriority;
	INIT_DECORATOR_NODE_NOTIFY_FLAGS();

	CooldownTag = Cooldown_Enemy_BasicAttack;
	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTD_IsMeleeAttackReady, BlackboardKey),
		AActor::StaticClass());
}

bool UBTD_IsMeleeAttackReady::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory) const
{
	const AAIController* AIController = OwnerComp.GetAIOwner();
	const ABaseEnemy* Enemy = AIController ? Cast<ABaseEnemy>(AIController->GetPawn()) : nullptr;
	const UBaseWeaponComponent* WeaponComponent = Enemy ? Enemy->GetWeaponComponent() : nullptr;
	const UAbilitySystemComponent* AbilitySystem = Enemy ? Enemy->GetAbilitySystemComponent() : nullptr;
	const UBlackboardComponent* BlackboardComponent = OwnerComp.GetBlackboardComponent();
	const AActor* TargetActor = BlackboardComponent
		? Cast<AActor>(BlackboardComponent->GetValueAsObject(GetSelectedBlackboardKey()))
		: nullptr;

	return IsValid(TargetActor)
		&& WeaponComponent
		&& WeaponComponent->IsWeaponEquipped()
		&& WeaponComponent->GetCurrentAttackRange() > KINDA_SMALL_NUMBER
		&& AbilitySystem
		&& !AbilitySystem->HasMatchingGameplayTag(CooldownTag)
		&& !AbilitySystem->HasMatchingGameplayTag(State_Attacking)
		&& !AbilitySystem->HasMatchingGameplayTag(State_Damaged);
}

void UBTD_IsMeleeAttackReady::OnBecomeRelevant(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	Super::OnBecomeRelevant(OwnerComp, NodeMemory);
	UnbindCooldownObserver();

	AAIController* AIController = OwnerComp.GetAIOwner();
	ABaseEnemy* Enemy = AIController ? Cast<ABaseEnemy>(AIController->GetPawn()) : nullptr;
	UAbilitySystemComponent* AbilitySystem = Enemy ? Enemy->GetAbilitySystemComponent() : nullptr;
	if (!AbilitySystem || !CooldownTag.IsValid())
	{
		return;
	}

	CachedOwnerComp = &OwnerComp;
	CachedAbilitySystem = AbilitySystem;
	CooldownTagDelegateHandle = AbilitySystem
		->RegisterGameplayTagEvent(CooldownTag, EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &UBTD_IsMeleeAttackReady::HandleCooldownTagChanged);
}

void UBTD_IsMeleeAttackReady::OnCeaseRelevant(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	UnbindCooldownObserver();
	Super::OnCeaseRelevant(OwnerComp, NodeMemory);
}

FString UBTD_IsMeleeAttackReady::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("Weapon equipped, target valid, and %s absent\nDistance handled by Move To Weapon Range"),
		*CooldownTag.ToString());
}

void UBTD_IsMeleeAttackReady::HandleCooldownTagChanged(FGameplayTag ChangedTag, int32 NewCount)
{
	if (UBehaviorTreeComponent* OwnerComp = CachedOwnerComp.Get())
	{
		ConditionalFlowAbort(*OwnerComp, EBTDecoratorAbortRequest::ConditionResultChanged);
	}
}

void UBTD_IsMeleeAttackReady::UnbindCooldownObserver()
{
	if (UAbilitySystemComponent* AbilitySystem = CachedAbilitySystem.Get();
		AbilitySystem && CooldownTagDelegateHandle.IsValid() && CooldownTag.IsValid())
	{
		AbilitySystem->RegisterGameplayTagEvent(CooldownTag, EGameplayTagEventType::NewOrRemoved)
			.Remove(CooldownTagDelegateHandle);
	}

	CooldownTagDelegateHandle.Reset();
	CachedAbilitySystem.Reset();
	CachedOwnerComp.Reset();
}
