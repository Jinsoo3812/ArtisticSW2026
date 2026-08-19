#include "RangedEnemy/RangedEnemyProjectile.h"

ARangedEnemyProjectile::ARangedEnemyProjectile()
{
	InitialLifeSpan = 10.0f;
	SetNetUpdateFrequency(30.0f);
	SetMinNetUpdateFrequency(20.0f);
}
