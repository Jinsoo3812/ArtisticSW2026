#include "Weapon/EnemyBow.h"

#include "BaseGameplayTags.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "RangedEnemy/RangedEnemyProjectile.h"

AEnemyBow::AEnemyBow()
{
	ArrowSocketPoint = CreateDefaultSubobject<USceneComponent>(TEXT("Arrow_socket"));
	ArrowSocketPoint->SetupAttachment(GetWeaponMesh());
	ArrowSocketPoint->SetRelativeLocation(FVector(75.0f, 0.0f, 0.0f));

	ProjectileClass = ARangedEnemyProjectile::StaticClass();
}

FGameplayTag AEnemyBow::GetEnemyBowWeaponTag()
{
	return Item_EnemyWeapon_Bow;
}

bool AEnemyBow::GetArrowSpawnTransform(FTransform& OutSpawnTransform) const
{
	OutSpawnTransform = FTransform::Identity;

	const UStaticMeshComponent* BowMesh = GetWeaponMesh();
	if (BowMesh && !ArrowSocketName.IsNone() && BowMesh->DoesSocketExist(ArrowSocketName))
	{
		OutSpawnTransform = BowMesh->GetSocketTransform(ArrowSocketName, RTS_World);
		return true;
	}

	if (!ArrowSocketPoint)
	{
		return false;
	}

	OutSpawnTransform = ArrowSocketPoint->GetComponentTransform();
	return true;
}

bool AEnemyBow::HasArrowSocket() const
{
	FTransform UnusedTransform;
	return GetArrowSpawnTransform(UnusedTransform);
}
