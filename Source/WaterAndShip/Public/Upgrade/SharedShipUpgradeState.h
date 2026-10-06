#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Upgrade/ShipUpgradeTypes.h"
#include "Room/SWVoyageResetParticipant.h"
#include "SharedShipUpgradeState.generated.h"

class AShip;
class UShipUpgradeComponent;

/**
 * Session-scoped authoritative upgrade state for the single shared player ship.
 * It survives replacement of the physical ship actor, but is intentionally not
 * persisted when the game process exits.
 */
UCLASS()
class WATERANDSHIP_API ASharedShipUpgradeState : public AActor, public ISWVoyageResetParticipant
{
	GENERATED_BODY()

public:
	ASharedShipUpgradeState();
	virtual ESWVoyagePolicy GetVoyagePolicy_Implementation() const override { return ESWVoyagePolicy::ResetParticipant; }
	virtual ESWVoyageRestoreStage GetVoyageRestoreStage_Implementation() const override { return ESWVoyageRestoreStage::SharedState; }
	virtual FName GetVoyageParticipantId_Implementation() const override;
	virtual ESWVoyageStepResult PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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
