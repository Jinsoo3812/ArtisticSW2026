#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "EnemyShipNavigationComponent.generated.h"

class AEnemyShip;
class AShip;

USTRUCT()
struct FEnemyShipHomeAlignment
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) bool bActive = false;
	UPROPERTY(SaveGame) bool bRestorePhysics = true;
	UPROPERTY(SaveGame) FTransform Start;
	UPROPERTY(SaveGame) FTransform Home;
	UPROPERTY(SaveGame) float Duration = 1.0f;
	UPROPERTY(SaveGame) float Elapsed = 0.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnEnemyShipNavigationStateChanged,
	ENavalCombatState, PreviousState,
	ENavalCombatState, NewState);

/**
 * Owns all persistent naval steering state and is the only Enemy-side writer
 * of AShip AI control input. BT and Gameplay Abilities submit intent only.
 */
UCLASS(ClassGroup = (EnemyShip), meta = (BlueprintSpawnableComponent))
class ENEMY_API UEnemyShipNavigationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemyShipNavigationComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Enemy Ship|Navigation")
	void SetNavigationEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Navigation")
	bool IsNavigationEnabled() const { return bNavigationEnabled; }

	UFUNCTION(BlueprintCallable, Category = "Enemy Ship|Navigation")
	void SetNavigationProfile(const FEnemyShipNavigationProfile& InProfile);

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Navigation")
	const FEnemyShipNavigationProfile& GetNavigationProfile() const { return NavigationProfile; }

	UFUNCTION(BlueprintCallable, Category = "Enemy Ship|Navigation")
	void SetTargetShip(AShip* InTargetShip);

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Navigation")
	AShip* GetTargetShip() const { return TargetShip; }

	/** Resolves the ship's server-captured spawn location. */
	bool GetResolvedHomeLocation(FVector& OutHomeLocation) const;
	bool GetSpawnHomeTransform(FTransform& OutTransform) const;
	void ResetSpawnHome(const FTransform& Transform);
	bool IsAligningHome() const { return HomeAlignment.bActive; }
	UPROPERTY(EditAnywhere, Category="Enemy Ship|Navigation", meta=(ClampMin="0.0", Units="s"))
	float HomeAlignmentDuration = 1.0f;

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Navigation")
	ENavalCombatState GetCurrentState() const { return CurrentState; }

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Navigation")
	FEnemyShipNavigationOutput GetLastNavigationOutput() const { return LastNavigationOutput; }

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Navigation|Avoidance")
	bool IsAvoidanceManeuverActive() const { return bAvoidanceManeuverActive; }

	FEnemyShipNavigationOverrideHandle AcquireOverride(
		UObject* Requester,
		int32 Priority,
		const FEnemyShipNavigationOverrideRequest& Request);

	bool UpdateOverride(
		FEnemyShipNavigationOverrideHandle Handle,
		const FEnemyShipNavigationOverrideRequest& Request);

	bool ReleaseOverride(FEnemyShipNavigationOverrideHandle Handle);
	void ReleaseOverridesFor(UObject* Requester);
	void ClearAllOverrides();
	bool HasActiveOverride() const;

	UPROPERTY(BlueprintAssignable, Category = "Enemy Ship|Navigation")
	FOnEnemyShipNavigationStateChanged OnNavigationStateChanged;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Enemy Ship|Navigation")
	FEnemyShipNavigationProfile NavigationProfile;

private:
	UFUNCTION()
	void OnRep_CurrentState(ENavalCombatState PreviousState);
	UFUNCTION() void OnRep_HomeAlignment();
	void TickHomeAlignment(float DeltaTime);

	struct FRuntimeOverride
	{
		TWeakObjectPtr<UObject> Requester;
		int32 Priority = 0;
		uint64 Sequence = 0;
		FEnemyShipNavigationOverrideRequest Request;
	};

	FEnemyShipNavigationContext BuildContext() const;
	void RemoveInvalidOverrides();
	const FRuntimeOverride* FindWinningOverride() const;
	void ApplyControl(const FEnemyShipNavigationOutput& BaseOutput);
	void UpdateAvoidance(float DeltaTime);
	void UpdateTerrainAvoidance(float DeltaTime);
	void ResetTerrainAvoidance();
	void ResetAvoidance();
	void StopOwnerShip();

	TWeakObjectPtr<AEnemyShip> OwnerShip;
	UPROPERTY(Replicated)
	TObjectPtr<AShip> TargetShip;

	UPROPERTY(Replicated)
	FVector SpawnHomeLocation = FVector::ZeroVector;
	UPROPERTY(Replicated)
	FRotator SpawnHomeRotation = FRotator::ZeroRotator;
	UPROPERTY(Replicated)
	bool bHasSpawnHomeLocation = false;
	TMap<FGuid, FRuntimeOverride> Overrides;
	uint64 NextOverrideSequence = 1;
	UPROPERTY(ReplicatedUsing = OnRep_CurrentState)
	ENavalCombatState CurrentState = ENavalCombatState::Idle;
	FEnemyShipNavigationOutput LastNavigationOutput;
	float LostTargetElapsed = 0.0f;
	float AvoidanceEvaluationAccumulator = 0.0f;
	float AvoidanceMinimumTimeRemaining = 0.0f;
	float AvoidanceSafeElapsed = 0.0f;
	bool bAvoidanceManeuverActive = false;
	bool bAvoidanceOverridesTurn = false;
	float AvoidanceTurnInput = 0.0f;
	TWeakObjectPtr<AActor> AvoidanceThreatActor;
	TArray<TWeakObjectPtr<AActor>> WaterBodies;
	bool bTerrainAvoidanceActive = false;
	bool bTerrainBothSidesBlocked = false;
	bool bTerrainOverlapping = false;
	bool bTerrainOverlapReverseExhausted = false;
	float TerrainMoveInput = 0.0f;
	float TerrainTurnInput = 0.0f;
	float TerrainTurnMinimumRemaining = 0.0f;
	float TerrainClearElapsed = 0.0f;
	float TerrainOverlapReverseRemaining = 0.0f;
	float TerrainEvaluationAccumulator = 0.0f;
	int32 TerrainSelectedSide = 0;
	TWeakObjectPtr<AActor> TerrainThreatActor;
	UPROPERTY(Replicated)
	bool bNavigationEnabled = true;
	UPROPERTY(ReplicatedUsing=OnRep_HomeAlignment, SaveGame)
	FEnemyShipHomeAlignment HomeAlignment;
	float AlignmentClientReceivedTime = 0.0f;
	bool bAlignmentPhysicsSuspended = false;
};
