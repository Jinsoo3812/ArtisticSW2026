#pragma once

#include "CoreMinimal.h"
#include "Storage/StorageChest.h"
#include "EnemyItemBox.generated.h"

/** Ship-mounted collectible box used as the authoritative boss encounter trigger. */
UCLASS(Blueprintable)
class ENEMY_API AEnemyItemBox : public AStorageChest
{
	GENERATED_BODY()

public:
	AEnemyItemBox();
};
