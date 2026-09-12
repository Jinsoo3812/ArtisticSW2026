#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Upgrade/ShipUpgradeTypes.h"
#include "SharedShipUpgradeState.generated.h"

class AShip;
class UShipUpgradeComponent;

/**
 * Session-scoped authoritative upgrade state for the single shared player ship.
 * It survives replacement of the physical ship actor, but is intentionally not
 * persisted when the game process exits.
 */
UCLASS()
class WATERANDSHIP_API ASharedShipUpgradeState : public AActor
{
	GENERATED_BODY()

public:
	ASharedShipUpgradeState();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Ship Upgrade|Shared")
	UShipUpgradeComponent* GetUpgradeComponent() const { return UpgradeComponent; }

	UFUNCTION(BlueprintPure, Category = "Ship Upgrade|Shared")
	AShip* GetCurrentPlayerShip() const { return CurrentPlayerShip; }

	/** Finds the one replicated state actor for this world. */
	UFUNCTION(BlueprintPure, Category = "Ship Upgrade|Shared", meta = (WorldContext = "WorldContextObject"))
	static ASharedShipUpgradeState* Find(const UObject* WorldContextObject);

	/** Server-only: points shared progression at a newly spawned physical ship. */
	void RegisterPlayerShip(AShip* Ship);
	void UnregisterPlayerShip(AShip* Ship);

private:
	UFUNCTION()
	void HandleSharedStatsChanged(FShipStatSnapshot NewStats);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship Upgrade|Shared", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UShipUpgradeComponent> UpgradeComponent;

	UPROPERTY(Replicated)
	TObjectPtr<AShip> CurrentPlayerShip;
};
