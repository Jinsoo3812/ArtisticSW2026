#include "AI/EnemyBehaviorSet.h"

#if WITH_EDITOR
void UEnemyBehaviorSet::ConfigureEditorSubtree(EEnemyAIState State, FGameplayTag InjectionTag, UBehaviorTree* Tree)
{
	for (FEnemyStateBehavior& Entry : StateBehaviors)
	{
		if (Entry.State == State) { Entry.InjectionTag = InjectionTag; Entry.Subtree = Tree; return; }
	}
	FEnemyStateBehavior& Entry = StateBehaviors.AddDefaulted_GetRef();
	Entry.State = State; Entry.InjectionTag = InjectionTag; Entry.Subtree = Tree;
}
#endif

UBehaviorTree* UEnemyBehaviorSet::FindSubtree(EEnemyAIState State) const
{
	for (const FEnemyStateBehavior& Entry : StateBehaviors)
	{
		if (Entry.State == State)
		{
			return Entry.Subtree;
		}
	}

	return nullptr;
}

