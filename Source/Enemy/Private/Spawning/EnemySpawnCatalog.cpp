#include "Spawning/EnemySpawnCatalog.h"

#include "BaseEnemy.h"

bool UEnemySpawnCatalog::FindDefinition(
	FGameplayTag EnemyTypeTag,
	FEnemySpawnCatalogEntry& OutDefinition) const
{
	if (!EnemyTypeTag.IsValid())
	{
		return false;
	}

	const FEnemySpawnCatalogEntry* Entry = Entries.FindByPredicate(
		[EnemyTypeTag](const FEnemySpawnCatalogEntry& Candidate)
		{
			return Candidate.EnemyTypeTag == EnemyTypeTag;
		});

	if (!Entry || !Entry->EnemyClass)
	{
		return false;
	}

	OutDefinition = *Entry;
	return true;
}
