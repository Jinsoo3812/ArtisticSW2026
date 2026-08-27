#include "Task/BTT_ActivateBossAbility.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "BaseGameplayTags.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BossAI/ShipBossEnemy.h"
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
	// Distance decorators are preconditions only. Once a Boss basic-attack
	// montage is committed, leaving weapon range must not cancel it. Hit/death
	// cancellation still reaches the GA directly through GAS tags/abilities.
	if (ActiveSpec && ActiveSpec->Ability
		&& PreservesCommittedAbilityOnBTAbort(ActiveSpec->Ability->GetClass()))
	{
		return false;
	}
	return Super::ShouldCancelAbilityOnAbort(ActiveSpec);
}

bool UBTT_ActivateBossAbility::PreservesCommittedAbilityOnBTAbort(
	const TSubclassOf<UGameplayAbility> AbilityClass)
{
	return AbilityClass && AbilityClass->IsChildOf(UGA_BossBasicAttack::StaticClass());
}

void UBTT_ActivateBossAbility::OnAbilityTaskFinished(EBTNodeResult::Type Result)
{
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
