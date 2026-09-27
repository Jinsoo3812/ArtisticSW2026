#include "Task/BTT_ActivateBossAbility.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "BaseGameplayAbility.h"
#include "BaseGameplayTags.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BossAI/ShipBossEnemy.h"
#include "GAS/Ability/Boss/BossGameplayAbility.h"
#include "GAS/Ability/Boss/GA_BossBasicAttack.h"
#include "Weapon/BaseWeapon.h"
#include "Weapon/BaseWeaponComponent.h"

UBTT_ActivateBossAbility::UBTT_ActivateBossAbility()
{
	NodeName = TEXT("Activate Boss Ability");
	DestinationPointKey.SelectedKeyName = TEXT("DestinationPointId");
	DestinationPointKey.AddIntFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTT_ActivateBossAbility, DestinationPointKey));
}

FString UBTT_ActivateBossAbility::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("Activate and await %s%s%s"),
		*AbilityAssetTag.ToString(),
		bRequirePreselectedDestination ? TEXT(" (requires destination)") : TEXT(""),
		bPreferCurrentWeaponAbility ? TEXT(" (prefer current weapon spec)") : TEXT(""));
}

const FGameplayAbilitySpec* UBTT_ActivateBossAbility::FindAbilitySpec(
	APawn& Pawn,
	const UAbilitySystemComponent& AbilitySystem) const
{
	const AShipBossEnemy* Boss = Cast<AShipBossEnemy>(&Pawn);
	if (!Boss)
	{
		return nullptr;
	}

	const UObject* CurrentWeapon = nullptr;
	if (const UBaseWeaponComponent* WeaponComponent = Boss->GetWeaponComponent())
	{
		CurrentWeapon = WeaponComponent->GetCurrentWeapon();
	}

	const FGameplayAbilitySpec* FirstMatch = nullptr;
	// Boss basic attacks are actor-owned and data-driven. Prefer that spec over
	// the legacy generic basic attack still granted by the physical sword.
	if (AbilityAssetTag == GameplayAbility_BasicAttack)
	{
		for (const FGameplayAbilitySpec& Spec : AbilitySystem.GetActivatableAbilities())
		{
			if (Spec.Ability && Spec.Ability->IsA<UGA_BossBasicAttack>()
				&& Spec.Ability->GetAssetTags().HasTagExact(AbilityAssetTag))
			{
				return &Spec;
			}
		}
	}
	for (const FGameplayAbilitySpec& Spec : AbilitySystem.GetActivatableAbilities())
	{
		if (!Spec.Ability || !Spec.Ability->GetAssetTags().HasTagExact(AbilityAssetTag))
		{
			continue;
		}

		if (!FirstMatch)
		{
			FirstMatch = &Spec;
		}
		if (bPreferCurrentWeaponAbility && CurrentWeapon && Spec.SourceObject.Get() == CurrentWeapon)
		{
			return &Spec;
		}
	}
	return FirstMatch;
}

bool UBTT_ActivateBossAbility::ValidateActivationContext(
	APawn& Pawn,
	const UAbilitySystemComponent& AbilitySystem) const
{
	const AShipBossEnemy* Boss = Cast<AShipBossEnemy>(&Pawn);
	return Boss && (!bRequirePreselectedDestination || Boss->GetDestinationPointId() != INDEX_NONE);
}

bool UBTT_ActivateBossAbility::ShouldCancelAbilityOnAbort(
	const FGameplayAbilitySpec* ActiveSpec) const
{
	// Decorators are activation preconditions. Atomic abilities explicitly opt
	// into surviving a later BT branch abort; hit/death cancellation still
	// reaches the active instance through GAS.
	const UBaseGameplayAbility* BossAbility = ActiveSpec
		? Cast<UBaseGameplayAbility>(ActiveSpec->Ability)
		: nullptr;
	if (BossAbility && BossAbility->ShouldSurviveBehaviorTreeAbort())
	{
		return false;
	}
	return Super::ShouldCancelAbilityOnAbort(ActiveSpec);
}

void UBTT_ActivateBossAbility::OnAbilityTaskFinished(EBTNodeResult::Type Result)
{
	const FGameplayAbilitySpec* ActiveSpec = GetActiveAbilitySpec();
	const UBossGameplayAbility* BossAbility = ActiveSpec
		? Cast<UBossGameplayAbility>(ActiveSpec->Ability)
		: nullptr;
	if (Result == EBTNodeResult::Aborted && BossAbility
		&& BossAbility->ShouldSurviveBehaviorTreeAbort()
		&& BossAbility->OwnsPreselectedDestinationAfterCommit())
	{
		return;
	}
	ResetDestinationState();
}

void UBTT_ActivateBossAbility::ResetDestinationState()
{
	if (!bClearDestinationWhenFinished)
	{
		return;
	}

	UBehaviorTreeComponent* OwnerComp = GetCachedOwnerComp();
	if (!OwnerComp)
	{
		return;
	}

	if (UBlackboardComponent* Blackboard = OwnerComp->GetBlackboardComponent();
		Blackboard && Blackboard->GetKeyID(DestinationPointKey.SelectedKeyName) != FBlackboard::InvalidKey)
	{
		Blackboard->SetValueAsInt(DestinationPointKey.SelectedKeyName, INDEX_NONE);
	}

	if (AShipBossEnemy* Boss = Cast<AShipBossEnemy>(GetCachedPawn()))
	{
		Boss->SetDestinationPointId(INDEX_NONE);
	}
}
