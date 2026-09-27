#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnemyTerritoryComponent.generated.h"

/**
 * Runtime territory assigned by an encounter spawner.
 * The component stores policy data only; the controller and BT remain responsible
 * for selecting states and issuing movement requests.
 */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UEnemyTerritoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemyTerritoryComponent();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Enemy|Territory")
	void InitializeTerritory(const FVector& InHomeLocation, float InPatrolRadius, float InCombatRadius);

	UFUNCTION(BlueprintPure, Category = "Enemy|Territory")
	bool HasAssignedTerritory() const { return bTerritoryAssigned; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Territory")
	const FVector& GetHomeLocation() const { return HomeLocation; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Territory")
	float GetPatrolRadius() const { return PatrolRadius; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Territory")
	float GetCombatRadius() const { return CombatRadius; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Territory")
	bool IsInsidePatrolArea(const FVector& WorldLocation) const;

	UFUNCTION(BlueprintPure, Category = "Enemy|Territory")
	bool IsInsideCombatArea(const FVector& WorldLocation) const;

private:
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Enemy|Territory", meta = (AllowPrivateAccess = "true"))
	FVector HomeLocation = FVector::ZeroVector;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Enemy|Territory", meta = (AllowPrivateAccess = "true", Units = "cm"))
	float PatrolRadius = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Enemy|Territory", meta = (AllowPrivateAccess = "true", Units = "cm"))
	float CombatRadius = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Enemy|Territory", meta = (AllowPrivateAccess = "true"))
	bool bTerritoryAssigned = false;
};
