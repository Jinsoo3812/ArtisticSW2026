#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Weapon/BaseWeapon.h"

#include "EnemyBow.generated.h"

class AArrowProjectile;
class USceneComponent;

/**
 * Minimal Enemy bow used by the ranged-enemy Behavior Tree/GAS pipeline.
 *
 * The bow owns projectile presentation data and, critically, the mesh socket
 * used as the authoritative arrow spawn origin. The attack ability remains
 * responsible for damage-spec construction and projectile spawning.
 */
UCLASS(Blueprintable)
class ENEMY_API AEnemyBow : public ABaseWeapon
{
	GENERATED_BODY()

public:
	AEnemyBow();

	UFUNCTION(BlueprintPure, Category = "Enemy Bow")
	static FGameplayTag GetEnemyBowWeaponTag();

	/** Resolves ArrowSocketName on WeaponMesh, then the Arrow_socket scene point. */
	UFUNCTION(BlueprintPure, Category = "Enemy Bow|Projectile")
	bool GetArrowSpawnTransform(FTransform& OutSpawnTransform) const;

	UFUNCTION(BlueprintPure, Category = "Enemy Bow|Projectile")
	bool HasArrowSocket() const;

	UFUNCTION(BlueprintPure, Category = "Enemy Bow|Projectile")
	FName GetArrowSocketName() const { return ArrowSocketName; }

	UFUNCTION(BlueprintPure, Category = "Enemy Bow|Projectile")
	TSubclassOf<AArrowProjectile> GetProjectileClass() const { return ProjectileClass; }

	UFUNCTION(BlueprintPure, Category = "Enemy Bow|Projectile")
	float GetProjectileSpeed() const { return FMath::Max(0.0f, ProjectileSpeed); }

protected:
	/** Must exist on the bow's WeaponMesh. The spelling is part of the MVP asset contract. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Bow|Projectile")
	FName ArrowSocketName = TEXT("Arrow_socket");

	/**
	 * Authorable fallback for meshes that do not contain a native socket yet.
	 * A real mesh socket named Arrow_socket takes precedence when present.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Bow|Projectile")
	TObjectPtr<USceneComponent> ArrowSocketPoint = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Bow|Projectile")
	TSubclassOf<AArrowProjectile> ProjectileClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Bow|Projectile", meta = (ClampMin = "1.0", Units = "cm/s"))
	float ProjectileSpeed = 2500.0f;
};
