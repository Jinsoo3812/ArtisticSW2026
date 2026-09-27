#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayEffectTypes.h"
#include "EnemyShipWeakeningWorldSubsystem.generated.h"

class AEnemyShip;
class ABaseEnemy;
class UBaseHealthComponent;
class UEnemyShipWeakeningData;

UCLASS()
class ENEMY_API UEnemyShipWeakeningWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	void RegisterShip(AEnemyShip* Ship);
	void UnregisterShip(AEnemyShip* Ship);
	void RegisterMember(AEnemyShip* Ship, ABaseEnemy* Member);
	void UnregisterMember(AEnemyShip* Ship, ABaseEnemy* Member);
	void BeforeMemberBaseStatsReset(ABaseEnemy* Member);
	void AfterMemberBaseStatsReset(ABaseEnemy* Member);
	void RefreshMember(ABaseEnemy* Member);
	void RefreshShip(AEnemyShip* Ship);

private:
	UFUNCTION()
	void HandleShipHealthChanged(UBaseHealthComponent* Health, float OldValue, float NewValue, AActor* InstigatorActor);
	UFUNCTION()
	void HandleMemberDeath(UBaseHealthComponent* Health);
	void RemoveMemberEffect(ABaseEnemy* Member);
	bool IsServerWorld() const;

	UPROPERTY(Transient)
	TObjectPtr<UEnemyShipWeakeningData> Data;
	bool bLoadedData = false;
	TMap<TWeakObjectPtr<AEnemyShip>, TWeakObjectPtr<UBaseHealthComponent>> ShipHealth;
	TMap<TWeakObjectPtr<ABaseEnemy>, TWeakObjectPtr<AEnemyShip>> MemberOwners;
	TMap<TWeakObjectPtr<ABaseEnemy>, FActiveGameplayEffectHandle> MemberEffects;
};
