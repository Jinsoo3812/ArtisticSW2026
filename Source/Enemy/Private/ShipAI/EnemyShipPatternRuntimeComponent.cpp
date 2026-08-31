#include "ShipAI/EnemyShipPatternRuntimeComponent.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ShipAI/EnemyShipPatternData.h"
#include "ShipAI/EnemyShipSkillModuleData.h"

UEnemyShipPatternRuntimeComponent::UEnemyShipPatternRuntimeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	RandomStream.Initialize(0);
}

void UEnemyShipPatternRuntimeComponent::SetPattern(UEnemyShipPatternData* InPattern)
{
	Pattern = InPattern;
	RebuildResolvedRules();
	ResetRuntimeState(0);
}

void UEnemyShipPatternRuntimeComponent::SetCoreSkillModules(
	const TArray<UEnemyShipSkillModuleData*>& InCoreModules)
{
	CoreSkillModules.Reset();
	for (UEnemyShipSkillModuleData* Module : InCoreModules)
	{
		if (IsValid(Module))
		{
			CoreSkillModules.AddUnique(Module);
		}
	}
	RebuildResolvedRules();
	ResetRuntimeState(0);
}

bool UEnemyShipPatternRuntimeComponent::SelectAbility(
	AActor* TargetActor,
	FEnemyShipAbilitySelection& OutSelection)
{
	const UWorld* World = GetWorld();
	return SelectAbilityAtTime(TargetActor, World ? World->GetTimeSeconds() : 0.0, OutSelection);
}

bool UEnemyShipPatternRuntimeComponent::SelectAbilityAtTime(
	AActor* TargetActor,
	double CurrentTimeSeconds,
	FEnemyShipAbilitySelection& OutSelection)
{
	OutSelection = FEnemyShipAbilitySelection();
	PendingRuleId = NAME_None;
	PendingSelectionTime = CurrentTimeSeconds;

	const AEnemyShip* Ship = Cast<AEnemyShip>(GetOwner());
	if (!Pattern || !Ship || !Ship->HasAuthority() || !IsValid(TargetActor))
	{
		return false;
	}

	FGameplayTagContainer OwnerTags;
	if (const UAbilitySystemComponent* ASC = Ship->GetAbilitySystemComponent())
	{
		ASC->GetOwnedGameplayTags(OwnerTags);
	}

	TArray<int32> EligibleIndices;
	for (int32 RuleIndex = 0; RuleIndex < ResolvedRules.Num(); ++RuleIndex)
	{
		if (IsRuleEligible(RuleIndex, TargetActor, CurrentTimeSeconds, OwnerTags))
		{
			EligibleIndices.Add(RuleIndex);
		}
	}

	const int32 SelectedIndex = SelectEligibleIndex(EligibleIndices);
	if (!ResolvedRules.IsValidIndex(SelectedIndex))
	{
		return false;
	}

	const FEnemyShipSkillRule& Rule = ResolvedRules[SelectedIndex];
	OutSelection.AbilityTag = Rule.AbilityTag;
	OutSelection.MovementPolicy = Rule.MovementPolicy;
	OutSelection.RuleId = Rule.RuleId;
	PendingRuleId = Rule.RuleId;
	return true;
}

bool UEnemyShipPatternRuntimeComponent::CommitSelection(const FEnemyShipAbilitySelection& Selection)
{
	const int32 RuleIndex = ResolvedRules.IndexOfByPredicate([&Selection](const FEnemyShipSkillRule& Rule)
	{
		return Rule.RuleId == Selection.RuleId;
	});
	if (!Pattern || Selection.RuleId != PendingRuleId
		|| !ResolvedRules.IsValidIndex(RuleIndex)
		|| ResolvedRules[RuleIndex].AbilityTag != Selection.AbilityTag)
	{
		return false;
	}

	LastCommittedTimes.FindOrAdd(Selection.RuleId) = PendingSelectionTime;
	if (ResolvedRules[RuleIndex].bUseOnlyOnce)
	{
		ConsumedOneShotRules.Add(Selection.RuleId);
	}
	if (Pattern->SelectionPolicy == EEnemyShipPatternSelectionPolicy::Sequence && !ResolvedRules.IsEmpty())
	{
		SequenceCursor = (RuleIndex + 1) % ResolvedRules.Num();
	}
	PendingRuleId = NAME_None;
	return true;
}

void UEnemyShipPatternRuntimeComponent::ResetRuntimeState(int32 RandomSeed)
{
	LastCommittedTimes.Reset();
	ConsumedOneShotRules.Reset();
	RandomStream.Initialize(RandomSeed);
	SequenceCursor = 0;
	PendingSelectionTime = 0.0;
	PendingRuleId = NAME_None;
}

double UEnemyShipPatternRuntimeComponent::GetLastCommittedTime(FName RuleId) const
{
	if (const double* Time = LastCommittedTimes.Find(RuleId))
	{
		return *Time;
	}
	return -1.0;
}

float UEnemyShipPatternRuntimeComponent::GetPendingTargetPredictionStrength(
	const FGameplayTag& AbilityTag) const
{
	const FEnemyShipSkillRule* Rule = ResolvedRules.FindByPredicate(
		[this, &AbilityTag](const FEnemyShipSkillRule& Candidate)
		{
			return Candidate.RuleId == PendingRuleId && Candidate.AbilityTag == AbilityTag;
		});
	return Rule ? FMath::Clamp(Rule->TargetPredictionStrength, 0.0f, 1.0f) : 0.0f;
}

bool UEnemyShipPatternRuntimeComponent::IsRuleEligible(
	int32 RuleIndex,
	AActor* TargetActor,
	double CurrentTimeSeconds,
	const FGameplayTagContainer& OwnerTags) const
{
	if (!Pattern || !ResolvedRules.IsValidIndex(RuleIndex)
		|| ConsumedOneShotRules.Contains(ResolvedRules[RuleIndex].RuleId))
	{
		return false;
	}

	const FEnemyShipSkillRule& Rule = ResolvedRules[RuleIndex];
	if (!Rule.AbilityTag.IsValid() || !IsValid(TargetActor))
	{
		return false;
	}
	if (!IsGrantedAbilityAvailable(Rule.AbilityTag))
	{
		return false;
	}
	if (!OwnerTags.HasAll(Rule.RequiredOwnerTags) || OwnerTags.HasAny(Rule.BlockedOwnerTags))
	{
		return false;
	}
	const AEnemyShip* Ship = Cast<AEnemyShip>(GetOwner());
	const UEnemyShipNavigationComponent* Navigation = Ship ? Ship->GetNavigationComponent() : nullptr;
	if (!Navigation || Navigation->GetCurrentState() == ENavalCombatState::Return
		|| Ship->IsCrewDefeated())
	{
		return false;
	}
	if (!Rule.AllowedNavigationStates.IsEmpty())
	{
		if (!Navigation || !Rule.AllowedNavigationStates.Contains(Navigation->GetCurrentState()))
		{
			return false;
		}
	}

	if (const double* LastTime = LastCommittedTimes.Find(Rule.RuleId))
	{
		if (CurrentTimeSeconds - *LastTime < Rule.MinimumInterval)
		{
			return false;
		}
	}
	return true;
}

bool UEnemyShipPatternRuntimeComponent::IsGrantedAbilityAvailable(const FGameplayTag& AbilityTag) const
{
	const AEnemyShip* Ship = Cast<AEnemyShip>(GetOwner());
	const UAbilitySystemComponent* ASC = Ship ? Ship->GetAbilitySystemComponent() : nullptr;
	const FGameplayAbilityActorInfo* ActorInfo = ASC ? ASC->AbilityActorInfo.Get() : nullptr;
	if (!ASC || !ActorInfo || !AbilityTag.IsValid())
	{
		return false;
	}

	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability
			&& Spec.Ability->GetAssetTags().HasTagExact(AbilityTag)
			&& Spec.Ability->CanActivateAbility(Spec.Handle, ActorInfo))
		{
			return true;
		}
	}
	return false;
}

int32 UEnemyShipPatternRuntimeComponent::SelectEligibleIndex(const TArray<int32>& EligibleIndices)
{
	if (!Pattern || EligibleIndices.IsEmpty())
	{
		return INDEX_NONE;
	}

	if (Pattern->SelectionPolicy == EEnemyShipPatternSelectionPolicy::Sequence)
	{
		for (int32 Offset = 0; Offset < ResolvedRules.Num(); ++Offset)
		{
			const int32 Candidate = (SequenceCursor + Offset) % ResolvedRules.Num();
			if (EligibleIndices.Contains(Candidate))
			{
				return Candidate;
			}
		}
		return INDEX_NONE;
	}

	if (Pattern->SelectionPolicy == EEnemyShipPatternSelectionPolicy::WeightedRandom)
	{
		float TotalWeight = 0.0f;
		for (const int32 Index : EligibleIndices)
		{
			TotalWeight += FMath::Max(0.0f, ResolvedRules[Index].Weight);
		}
		if (TotalWeight <= KINDA_SMALL_NUMBER)
		{
			return INDEX_NONE;
		}

		float Roll = RandomStream.FRandRange(0.0f, TotalWeight);
		for (const int32 Index : EligibleIndices)
		{
			Roll -= FMath::Max(0.0f, ResolvedRules[Index].Weight);
			if (Roll <= 0.0f)
			{
				return Index;
			}
		}
		return EligibleIndices.Last();
	}

	int32 BestIndex = EligibleIndices[0];
	for (const int32 Index : EligibleIndices)
	{
		if (ResolvedRules[Index].Priority > ResolvedRules[BestIndex].Priority)
		{
			BestIndex = Index;
		}
	}
	return BestIndex;
}

void UEnemyShipPatternRuntimeComponent::RebuildResolvedRules()
{
	ResolvedRules.Reset();
	TSet<FName> SeenModuleIds;
	TSet<FName> SeenRuleIds;
	TSet<FGameplayTag> SeenAbilityTags;

	auto AppendModule = [this, &SeenModuleIds, &SeenRuleIds, &SeenAbilityTags](
		const UEnemyShipSkillModuleData* Module)
	{
		if (!IsValid(Module) || Module->ModuleId.IsNone() || SeenModuleIds.Contains(Module->ModuleId))
		{
			return;
		}
		SeenModuleIds.Add(Module->ModuleId);
		for (const FEnemyShipSkillRule& Rule : Module->SkillRules)
		{
			if (Rule.RuleId.IsNone() || !Rule.AbilityTag.IsValid()
				|| SeenRuleIds.Contains(Rule.RuleId) || SeenAbilityTags.Contains(Rule.AbilityTag))
			{
				continue;
			}
			SeenRuleIds.Add(Rule.RuleId);
			SeenAbilityTags.Add(Rule.AbilityTag);
			ResolvedRules.Add(Rule);
		}
	};

	for (const UEnemyShipSkillModuleData* Module : CoreSkillModules)
	{
		AppendModule(Module);
	}
	if (Pattern)
	{
		for (const UEnemyShipSkillModuleData* Module : Pattern->SkillModules)
		{
			AppendModule(Module);
		}
	}
}
