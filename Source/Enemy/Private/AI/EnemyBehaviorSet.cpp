#include "AI/EnemyBehaviorSet.h"

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

