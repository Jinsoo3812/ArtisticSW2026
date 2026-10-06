#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayEffectTypes.h"
#include "Room/SWVoyageResetParticipant.h"
#include "EnemyShipWeakeningWorldSubsystem.generated.h"

class AEnemyShip;
class ABaseEnemy;
class UBaseHealthComponent;
class UEnemyShipWeakeningData;

UCLASS()
class ENEMY_API UEnemyShipWeakeningWorldSubsystem : public UWorldSubsystem, public ISWVoyageResetParticipant
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	virtual ESWVoyagePolicy GetVoyagePolicy_Implementation() const override;
	virtual FName GetVoyageParticipantId_Implementation() const override;
	virtual ESWVoyageStepResult PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual void ResumeVoyage_Implementation(const FSWVoyageResetContext& Context) override;
	virtual void CancelVoyagePreparation_Implementation(const FSWVoyageResetContext& Context) override;
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
	void ClearWorldBindings();
	bool bVoyageEventsDeferred = false;

	UPROPERTY(Transient)
	TObjectPtr<UEnemyShipWeakeningData> Data;
	bool bLoadedData = false;
	TMap<TWeakObjectPtr<AEnemyShip>, TWeakObjectPtr<UBaseHealthComponent>> ShipHealth;
	TMap<TWeakObjectPtr<ABaseEnemy>, TWeakObjectPtr<AEnemyShip>> MemberOwners;
	TMap<TWeakObjectPtr<ABaseEnemy>, FActiveGameplayEffectHandle> MemberEffects;
};
