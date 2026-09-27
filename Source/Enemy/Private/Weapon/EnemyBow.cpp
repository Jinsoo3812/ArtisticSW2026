#include "Weapon/EnemyBow.h"

#include "BaseGameplayTags.h"
#include "RangedEnemy/RangedEnemyProjectile.h"

AEnemyBow::AEnemyBow()
{
	ProjectileClass = ARangedEnemyProjectile::StaticClass();
}

FGameplayTag AEnemyBow::GetEnemyBowWeaponTag()
{
	return Item_EnemyWeapon_Bow;
}
