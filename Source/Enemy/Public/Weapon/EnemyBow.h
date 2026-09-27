#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Weapon/BaseWeapon.h"

#include "EnemyBow.generated.h"

class AArrowProjectile;

/**
 * Minimal Enemy bow used by the ranged-enemy Behavior Tree/GAS pipeline.
 *
 * The bow owns projectile presentation and launch data. The ranged enemy's
 * character mesh owns the authoritative arrow spawn origin.
 */
UCLASS(Blueprintable)
class ENEMY_API AEnemyBow : public ABaseWeapon
{
	GENERATED_BODY()

public:
	AEnemyBow();

	UFUNCTION(BlueprintPure, Category = "Enemy Bow")
	static FGameplayTag GetEnemyBowWeaponTag();

	UFUNCTION(BlueprintPure, Category = "Enemy Bow|Projectile")
	TSubclassOf<AArrowProjectile> GetProjectileClass() const { return ProjectileClass; }

	UFUNCTION(BlueprintPure, Category = "Enemy Bow|Projectile")
	float GetProjectileSpeed() const { return FMath::Max(0.0f, ProjectileSpeed); }

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Bow|Projectile")
	TSubclassOf<AArrowProjectile> ProjectileClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Bow|Projectile", meta = (ClampMin = "1.0", Units = "cm/s"))
	float ProjectileSpeed = 2500.0f;
};
