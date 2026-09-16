#include "ShipAI/EnemyShipPatternRuntimeComponent.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipArchetypeData.h"
#include "ShipAI/EnemyShipNavigationComponent.h"

UEnemyShipPatternRuntimeComponent::UEnemyShipPatternRuntimeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	RandomStream.Initialize(0);
}

void UEnemyShipPatternRuntimeComponent::Configure(UEnemyShipArchetypeData* InArchetype)
{
	Archetype = InArchetype;
	SkillModules.Reset();
	if (Archetype)
	{
		for (UEnemyShipSkillModuleData* Module : Archetype->SkillModules)
		{
			if (IsValid(Module))
			{
				SkillModules.AddUnique(Module);
			}
		}
	}
	ResetRuntimeState(0);
}

bool UEnemyShipPatternRuntimeComponent::SelectAbility(AActor* TargetActor, FEnemyShipAbilitySelection& OutSelection)
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
	PendingModule.Reset();
	const AEnemyShip* Ship = Cast<AEnemyShip>(GetOwner());
	if (!Archetype || !Ship || !Ship->HasAuthority() || !IsValid(TargetActor))
	{
		return false;
	}

	FGameplayTagContainer OwnerTags;
	if (const UAbilitySystemComponent* ASC = Ship->GetAbilitySystemComponent())
	{
		ASC->GetOwnedGameplayTags(OwnerTags);
	}

	TArray<int32> EligibleIndices;
	for (int32 Index = 0; Index < SkillModules.Num(); ++Index)
	{
		if (IsModuleEligible(Index, TargetActor, OwnerTags))
		{
			EligibleIndices.Add(Index);
		}
	}
	const int32 SelectedIndex = SelectEligibleIndex(EligibleIndices);
	if (!SkillModules.IsValidIndex(SelectedIndex))
	{
		return false;
	}

	const UEnemyShipSkillModuleData* Module = SkillModules[SelectedIndex];
	OutSelection.AbilityTag = Module->GetAbilityTag();
	OutSelection.MovementPolicy = Module->MovementPolicy;
	OutSelection.RuleId = Module->GetFName();
	PendingModule = Module;
	return OutSelection.IsValid();
}

bool UEnemyShipPatternRuntimeComponent::CommitSelection(const FEnemyShipAbilitySelection& Selection)
{
	const UEnemyShipSkillModuleData* Module = PendingModule.Get();
	if (!Module || Selection.RuleId != Module->GetFName() || Selection.AbilityTag != Module->GetAbilityTag())
	{
		return false;
	}
	if (Module->bUseOnlyOnce)
	{
		ConsumedOneShotModules.Add(Module);
	}
	if (Archetype && Archetype->SelectionPolicy == EEnemyShipSkillSelectionPolicy::Sequence && !SkillModules.IsEmpty())
	{
		SequenceCursor = (SkillModules.IndexOfByKey(Module) + 1) % SkillModules.Num();
	}
	PendingModule.Reset();
	return true;
}

void UEnemyShipPatternRuntimeComponent::ResetRuntimeState(int32 RandomSeed)
{
	ConsumedOneShotModules.Reset();
	RandomStream.Initialize(RandomSeed);
	SequenceCursor = 0;
	PendingModule.Reset();
}

bool UEnemyShipPatternRuntimeComponent::IsModuleEligible(
	int32 ModuleIndex,
	AActor* TargetActor,
	const FGameplayTagContainer& OwnerTags) const
{
	if (!SkillModules.IsValidIndex(ModuleIndex) || !IsValid(TargetActor))
	{
		return false;
	}
	const UEnemyShipSkillModuleData* Module = SkillModules[ModuleIndex];
	if (!Module || ConsumedOneShotModules.Contains(Module)
		|| !OwnerTags.HasAll(Module->RequiredOwnerTags) || OwnerTags.HasAny(Module->BlockedOwnerTags))
	{
		return false;
	}
	const FGameplayTag AbilityTag = Module->GetAbilityTag();
	if (!AbilityTag.IsValid() || !IsGrantedAbilityAvailable(AbilityTag))
	{
		return false;
	}
	const AEnemyShip* Ship = Cast<AEnemyShip>(GetOwner());
	const UEnemyShipNavigationComponent* Navigation = Ship ? Ship->GetNavigationComponent() : nullptr;
	if (!Navigation || Navigation->GetCurrentState() == ENavalCombatState::Return || Ship->IsCrewDefeated())
	{
		return false;
	}
	return Module->AllowedNavigationStates.IsEmpty()
		|| Module->AllowedNavigationStates.Contains(Navigation->GetCurrentState());
}

bool UEnemyShipPatternRuntimeComponent::IsGrantedAbilityAvailable(const FGameplayTag& AbilityTag) const
{
	const AEnemyShip* Ship = Cast<AEnemyShip>(GetOwner());
	const UAbilitySystemComponent* ASC = Ship ? Ship->GetAbilitySystemComponent() : nullptr;
	const FGameplayAbilityActorInfo* ActorInfo = ASC ? ASC->AbilityActorInfo.Get() : nullptr;
	if (!ASC || !ActorInfo)
	{
		return false;
	}
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetAssetTags().HasTagExact(AbilityTag)
			&& Spec.Ability->CanActivateAbility(Spec.Handle, ActorInfo))
		{
			return true;
		}
	}
	return false;
}

int32 UEnemyShipPatternRuntimeComponent::SelectEligibleIndex(const TArray<int32>& EligibleIndices)
{
	if (!Archetype || EligibleIndices.IsEmpty())
	{
		return INDEX_NONE;
	}
	if (Archetype->SelectionPolicy == EEnemyShipSkillSelectionPolicy::Sequence)
	{
		for (int32 Offset = 0; Offset < SkillModules.Num(); ++Offset)
		{
			const int32 Candidate = (SequenceCursor + Offset) % SkillModules.Num();
			if (EligibleIndices.Contains(Candidate))
			{
				return Candidate;
			}
		}
		return INDEX_NONE;
	}
	if (Archetype->SelectionPolicy == EEnemyShipSkillSelectionPolicy::WeightedRandom)
	{
		float TotalWeight = 0.0f;
		for (int32 Index : EligibleIndices)
		{
			TotalWeight += FMath::Max(0.0f, SkillModules[Index]->Weight);
		}
		if (TotalWeight <= KINDA_SMALL_NUMBER)
		{
			return INDEX_NONE;
		}
		float Roll = RandomStream.FRandRange(0.0f, TotalWeight);
		for (int32 Index : EligibleIndices)
		{
			Roll -= FMath::Max(0.0f, SkillModules[Index]->Weight);
			if (Roll <= 0.0f)
			{
				return Index;
			}
		}
		return EligibleIndices.Last();
	}

	int32 BestIndex = EligibleIndices[0];
	for (int32 Index : EligibleIndices)
	{
		if (SkillModules[Index]->Priority > SkillModules[BestIndex]->Priority)
		{
			BestIndex = Index;
		}
	}
	return BestIndex;
}
