#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyShipTimeStopField.generated.h"

class ABasePlayer;
class APlayerController;
class AShip;
class UPhysicsConstraintComponent;
class USceneComponent;

UENUM()
enum class EEnemyShipTimeStopTargetType : uint8
{
	PlayerShip,
	PlayerCharacter
};

USTRUCT()
struct FEnemyShipTimeStopTarget
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AActor> Actor = nullptr;

	UPROPERTY()
	FTransform Anchor = FTransform::Identity;

	UPROPERTY()
	EEnemyShipTimeStopTargetType Type = EEnemyShipTimeStopTargetType::PlayerCharacter;
};

/** Replicated area effect that freezes ships through external world constraints and players through local input/movement suppression. */
UCLASS(Blueprintable)
class ENEMY_API AEnemyShipTimeStopField : public AActor
{
	GENERATED_BODY()

public:
	AEnemyShipTimeStopField();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitializeTimeStop(float InRadius, float InDurationSeconds);

	const TArray<FEnemyShipTimeStopTarget>& GetAffectedTargetsForDiagnostics() const
	{
		return AffectedTargets;
	}

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Time Stop")
	float GetEffectRadius() const { return EffectRadius; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Debug")
	bool bDrawDebugSphere = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Debug")
	FColor DebugSphereColor = FColor::Cyan;

private:
	struct FPlayerRuntimeState
	{
		TWeakObjectPtr<ABasePlayer> Player;
		TWeakObjectPtr<APlayerController> Controller;
		TWeakObjectPtr<APawn> ControlledPawn;
		uint8 SavedMovementMode = 0;
		uint8 SavedCustomMovementMode = 0;
		bool bMovementSuppressed = false;
		bool bInputSuppressed = false;
		bool bTagApplied = false;
		bool bAbilitiesCancelled = false;
		bool bCapturedBaseline = false;
	};

	UFUNCTION()
	void OnRep_AffectedTargets();

	void GatherAffectedTargets();
	void ApplyAllTargets();
	void ApplyShipTarget(const FEnemyShipTimeStopTarget& Target);
	void ApplyPlayerTarget(const FEnemyShipTimeStopTarget& Target);
	void FinishTimeStop();
	void TransferPlayerBaseline(ABasePlayer* Player, const FPlayerRuntimeState& Runtime);
	void ReleaseAllTargets();
	APlayerController* FindControllerForPlayer(const ABasePlayer* Player) const;
	UPhysicsConstraintComponent* CreateWorldLock(AShip* Ship, const FTransform& Anchor);

	UPROPERTY(ReplicatedUsing = OnRep_AffectedTargets)
	TArray<FEnemyShipTimeStopTarget> AffectedTargets;

	UPROPERTY(Replicated)
	float EffectRadius = 1500.0f;

	UPROPERTY(Replicated)
	float EffectDurationSeconds = 3.0f;

	UPROPERTY(Replicated)
	FGuid FreezeSourceId;

	TMap<TWeakObjectPtr<AShip>, TObjectPtr<UPhysicsConstraintComponent>> ShipConstraints;
	TSet<TWeakObjectPtr<AShip>> TaggedShips;
	TMap<TWeakObjectPtr<ABasePlayer>, FPlayerRuntimeState> PlayerRuntimeStates;
	FTimerHandle ExpirationTimerHandle;
	bool bReleased = false;
};
