#include "BossAI/EnemyItemBox.h"

AEnemyItemBox::AEnemyItemBox()
{
	bEnablePhysicsAndBuoyancy = false;
	StorageName = NSLOCTEXT("BossEncounter", "EnemyItemBoxName", "Enemy Item Box");
	ActionText = NSLOCTEXT("BossEncounter", "EnemyItemBoxCollect", "Collect");
	LockedActionText = NSLOCTEXT("BossEncounter", "EnemyItemBoxLocked", "Defeat Boss");
}
