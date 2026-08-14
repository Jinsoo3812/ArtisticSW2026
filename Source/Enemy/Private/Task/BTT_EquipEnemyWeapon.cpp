#include "Task/BTT_EquipEnemyWeapon.h"

#include "AIController.h"
#include "BaseEnemy.h"
#include "Weapon/BaseWeaponComponent.h"

UBTT_EquipEnemyWeapon::UBTT_EquipEnemyWeapon()
{
	NodeName = TEXT("Equip Enemy Weapon");
}

EBTNodeResult::Type UBTT_EquipEnemyWeapon::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	ABaseEnemy* Enemy = AIController ? Cast<ABaseEnemy>(AIController->GetPawn()) : nullptr;
	UBaseWeaponComponent* WeaponComponent = Enemy ? Enemy->GetWeaponComponent() : nullptr;
	if (!Enemy || !Enemy->HasAuthority() || !WeaponComponent || !WeaponComponent->GetCurrentWeapon())
	{
		return EBTNodeResult::Failed;
	}

	if (!WeaponComponent->IsWeaponEquipped())
	{
		WeaponComponent->EquipCurrentWeapon();
	}

	return WeaponComponent->IsWeaponEquipped()
		? EBTNodeResult::Succeeded
		: EBTNodeResult::Failed;
}
