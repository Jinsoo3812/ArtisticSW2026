#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "EnemyShipNavigationComponent.generated.h"

class AEnemyShip;
class AShip;

enum class EEnemyShipLocalSafetyState : uint8 { Normal, Waiting, Escaping };
enum class EEnemyShipEscapeAction : uint8 { Forward, ForwardRight, ForwardLeft, Reverse, ReverseRight, ReverseLeft };

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
	bool IsAvoidanceManeuverActive() const { return SafetyState != EEnemyShipLocalSafetyState::Normal; }
	void ResetLocalSafety();
	void NotifyChargeTerrainStall();

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
	using FRequestedControl = FEnemyShipNavigationOverrideRequest;
	FRequestedControl ResolveRequestedControl(const FEnemyShipNavigationOutput& BaseOutput) const;
	void ApplyControl(const FEnemyShipNavigationOutput& BaseOutput, float DeltaTime);
	void EnterSafetyWait(const TCHAR* Reason = TEXT("Unspecified"));
	FRequestedControl EscapeControl(EEnemyShipEscapeAction Action) const;
	bool IsHomePathSafe(const FTransform& Start, const FTransform& End) const;
	bool IsCombatSafetyLogEnabled() const;
	void LogCombatSafety(const TCHAR* Event, const TCHAR* Reason, const FString& Details = FString(), bool bSnapshot = false);
	void TickCombatSafetyDiagnostics(float DeltaTime);
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
	EEnemyShipLocalSafetyState SafetyState = EEnemyShipLocalSafetyState::Normal;
	EEnemyShipEscapeAction EscapeAction = EEnemyShipEscapeAction::Forward;
	FRandomStream SafetyRandom;
	float RetryRemaining = 0.0f;
	float SafeResumeElapsed = 0.0f;
	float ForwardResumeElapsed = 0.0f;
	float SafetyEvaluationAccumulator = 0.0f;
	float EscapeReconsiderAccumulator = 0.0f;
	float ProgressElapsed = 0.0f;
	float ProgressStartYaw = 0.0f;
	float TerrainProbeAccumulator = 0.0f;
	bool bTerrainProbeInitialized = false;
	bool bTerrainForwardClear = true;
	bool bTerrainPriority = false;
	float EscapeElapsed = 0.0f;
	FVector ProgressStart = FVector::ZeroVector;
	float EscapeLastYaw = 0.0f;
	float EscapeTurned = 0.0f;
	bool bPreferLeftEscape = false;
	bool bPendingHomeAlignmentSafety = false;
	double CombatLastTickWall = 0.0;
	double CombatLastSnapshotWall = 0.0;
	double CombatLogWindowStart = 0.0;
	float CombatWallGapMs = 0.0f;
	float CombatLastTickMs = 0.0f;
	float CombatDeltaTimeMs = 0.0f;
	float CombatQueryBuildMs = 0.0f;
	float CombatChecksMs = 0.0f;
	int32 CombatCheckCount = 0;
	int32 CombatLogLines = 0;
	uint64 CombatSuppressedLines = 0;
	const TCHAR* CombatLastCheckReason = TEXT("NotChecked");
	const TCHAR* CombatLastResumeReason = TEXT("NotChecked");
	TWeakObjectPtr<AActor> CombatLastBlocker;
	bool bCombatAppliedControlValid = false;
	float CombatAppliedMove = 0.0f;
	float CombatAppliedTurn = 0.0f;
	uint8 CombatAppliedMode = 0;
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
