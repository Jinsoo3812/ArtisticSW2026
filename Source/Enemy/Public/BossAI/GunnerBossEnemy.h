#pragma once
#include "CoreMinimal.h"
#include "BossAI/ShipBossEnemy.h"
#include "GunnerBossEnemy.generated.h"

class UGunnerBossCombatData;
class UBossGunCombatComponent;
class UBossGunMovementComponent;

/** Encounter/death stay in ShipBossEnemy; this pawn only composes the gunner's systems. */
UCLASS(Blueprintable)
class ENEMY_API AGunnerBossEnemy : public AShipBossEnemy
{
	GENERATED_BODY()
public:
	AGunnerBossEnemy(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gunner Boss") TObjectPtr<UGunnerBossCombatData> CombatData;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gunner Boss") TObjectPtr<UBossGunCombatComponent> GunCombat;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gunner Boss") TObjectPtr<UBossGunMovementComponent> GunMovement;
	virtual bool CanEngageActor_Implementation(AActor* Candidate) const override;
	virtual bool CanMoveOnDeck() const override;
	virtual bool RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError) override;
protected:
	virtual void BeginPlay() override;
	virtual void HandleDeath_Implementation() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	void CancelGunAbilities();
};
