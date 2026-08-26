// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MultiGameMode.h"
#include "GameFramework/SWGamePhaseTypes.h"
#include "Interactable/WaveFlowInterface.h"
#include "WaveGameMode.generated.h"

class ASWWaveGameState;
class AController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSWGameStarted);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnSWWaveStartRequested,
	int32,
	DisplayWaveNumber
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnSWWaveStopRequested,
	int32,
	DisplayWaveNumber,
	EWaveEndReason,
	EndReason
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnSWEnemySpawnCountdownStarted,
	int32,
	DisplayWaveNumber,
	float,
	CountdownDuration
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnSWWaveRuntimeStarted,
	int32,
	WaveArrayIndex,
	int32,
	DisplayWaveNumber,
	float,
	WaveTimeLimit
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnSWWaveEnded,
	int32,
	DisplayWaveNumber,
	EWaveEndReason,
	EndReason
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnSWIntermissionStarted,
	int32,
	NextDisplayWaveNumber,
	float,
	Duration
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSWIntermissionEnded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSWGameVictory);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSWGameDefeat);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnSWPhaseChanged,
	EGamePhase,
	NewPhase
);

/**
 * Wave 기반 게임 흐름을 관리하는 GameMode.
 *
 * 이 클래스의 핵심 책임:
 * - 플레이어 준비 완료 후 게임 흐름 시작
 * - Wave 시작 요청
 * - WaveSpawnManager가 보고한 Wave 진행 상황 수신
 * - Phase 전환
 * - GameState 동기화
 * - Victory / Defeat 판정
 *
 * 이 클래스가 하지 않는 것:
 * - WaveDataAsset 직접 참조
 * - Enemy 직접 Spawn
 * - SpawnGroup 파싱
 * - SpawnRoute 관리
 * - Enemy Death Delegate 직접 추적
 */
UCLASS()
class ARTISTICSWCORE_API AWaveGameMode
	: public AMultiGameMode
	, public IWaveFlowInterface
{
	GENERATED_BODY()

public:
	AWaveGameMode();

protected:
	virtual void BeginPlay() override;

	/**
	 * ASWMultiGameMode에서 모든 플레이어 Ready가 완료되었을 때 호출.
	 */
	virtual void HandleAllPlayersReady() override;
	virtual void HandleAllPlayersDeathFinished() override;

public:
	// =========================================================
	// GameMode → WaveSpawnManager 요청 Delegate
	// =========================================================

	/**
	 * GameMode가 WaveSystem에게 특정 DisplayWaveNumber의 Wave 시작을 요청한다.
	 *
	 * AWaveSpawnManager는 이 Delegate를 구독한 뒤,
	 * WaveDataAsset에서 해당 Wave를 찾아 PreWaveDelay부터 실행한다.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Wave|Request")
	FOnSWWaveStartRequested OnWaveStartRequested;

	/**
	 * GameMode가 현재 WaveSystem에게 Wave 중단을 요청한다.
	 * 
	 * 예:
	 * - 서버 관리자가 강제 종료
	 */
	UPROPERTY(BlueprintAssignable, Category = "Wave|Request")
	FOnSWWaveStopRequested OnWaveStopRequested;

public:
	// =========================================================
	// UI / Blueprint 알림 Delegate
	// =========================================================

	UPROPERTY(BlueprintAssignable, Category = "Wave|Event")
	FOnSWGameStarted OnGameStarted;

	UPROPERTY(BlueprintAssignable, Category = "Wave|Event")
	FOnSWEnemySpawnCountdownStarted OnEnemySpawnCountdownStarted;

	UPROPERTY(BlueprintAssignable, Category = "Wave|Event")
	FOnSWWaveRuntimeStarted OnWaveRuntimeStarted;

	UPROPERTY(BlueprintAssignable, Category = "Wave|Event")
	FOnSWWaveEnded OnWaveEnded;

	UPROPERTY(BlueprintAssignable, Category = "Wave|Event")
	FOnSWIntermissionStarted OnIntermissionStarted;

	UPROPERTY(BlueprintAssignable, Category = "Wave|Event")
	FOnSWIntermissionEnded OnIntermissionEnded;

	UPROPERTY(BlueprintAssignable, Category = "Wave|Event")
	FOnSWGameVictory OnGameVictory;

	UPROPERTY(BlueprintAssignable, Category = "Wave|Event")
	FOnSWGameDefeat OnGameDefeat;

	UPROPERTY(BlueprintAssignable, Category = "Wave|Event")
	FOnSWPhaseChanged OnPhaseChanged;

protected:
	// =========================================================
	// Game Rule Config
	// Wave 데이터가 아니라, 게임 규칙에 해당하는 값만 둔다.
	// =========================================================

	/**
	 * 플레이어 사망 시 패배 판정 규칙.
	 * WaveData가 아니라 Match Rule이므로 GameMode에 두는 것이 적절하다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wave|Rule")
	EFailConditionType FailCondition = EFailConditionType::AnyPlayerDead;

	/**
	 * 모든 플레이어가 Ready 되었을 때 자동으로 StartGameFlow를 호출할지 여부.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wave|Rule")
	bool bAutoStartGameFlowWhenReady = true;

	/**
	 * Intermission이 끝난 뒤 자동으로 다음 Wave를 요청할지 여부.
	 *
	 * true:
	 * - WaveDataAsset의 PostWaveDelay가 끝나면 다음 Wave 요청
	 *
	 * false:
	 * - Intermission 상태에서 멈춤
	 * - 외부에서 RequestStartNextWave를 직접 호출해야 함
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wave|Rule")
	bool bAutoRequestNextWaveAfterIntermission = true;

protected:
	// =========================================================
	// Runtime State
	// =========================================================

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wave|Runtime")
	int32 CurrentWave = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wave|Runtime")
	int32 TotalWaveCount = 0;
	
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wave|Runtime")
	EGamePhase CurrentPhase = EGamePhase::None;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wave|Runtime")
	bool bGameEnded = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wave|Runtime")
	bool bGameFlowStarted = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wave|Runtime")
	int32 AliveEnemyCount = 0;
	
	FTimerHandle IntermissionTimerHandle;

public:
	// =========================================================
	// Flow API
	// =========================================================

	UFUNCTION(BlueprintCallable, Category = "Wave|Flow")
	void StartGameFlow();

	/**
	 * 다음 Wave 시작을 요청한다.
	 * 실제 PreWaveDelay / SpawnGroup 실행은 AWaveSpawnManager가 담당한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wave|Flow")
	void RequestStartNextWave();

	/**
	 * 현재 Wave 중단 요청.
	 * GameMode는 Enemy를 직접 제거하지 않고, WaveSpawnManager에게 중단 요청 Delegate를 보낸다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wave|Flow")
	void RequestStopCurrentWave(EWaveEndReason EndReason);

	UFUNCTION(BlueprintCallable, Category = "Wave|Flow")
	void EndIntermission();

	UFUNCTION(BlueprintCallable, Category = "Wave|Flow")
	void HandleVictory();

	UFUNCTION(BlueprintCallable, Category = "Wave|Flow")
	void HandleDefeat();

	/**
	 * 플레이어 사망 시 외부에서 호출.
	 * Player / PlayerController / Character 쪽에서 서버 권한으로 호출하는 구조를 권장한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wave|Notify")
	void NotifyPlayerDead(AController* DeadPlayer);

public:
	// =========================================================
	// ISWWaveFlowInterface
	// WaveSpawnManager → GameMode 보고 함수
	// =========================================================

	virtual void NotifyWaveDataReady_Implementation(int32 InTotalWaveCount) override;

	virtual void NotifyWaveCountdownStarted_Implementation(
		int32 DisplayWaveNumber,
		float CountdownDuration
	) override;

	virtual void NotifyWaveRuntimeStarted_Implementation(
		int32 WaveArrayIndex,
		int32 DisplayWaveNumber,
		float WaveTimeLimit
	) override;

	virtual void NotifyWaveEnemyCountChanged_Implementation(
		int32 NewAliveEnemyCount
	) override;

	virtual void NotifyWaveCompleted_Implementation(
		int32 WaveArrayIndex,
		int32 DisplayWaveNumber,
		bool bSuccess,
		float PostWaveDelay
	) override;

	virtual void NotifyWaveStopped_Implementation(
		int32 WaveArrayIndex,
		int32 DisplayWaveNumber,
		EWaveEndReason EndReason,
		float PostWaveDelay
	) override;

public:
	// =========================================================
	// Getter
	// =========================================================

	UFUNCTION(BlueprintPure, Category = "Wave|State")
	int32 GetCurrentWave() const { return CurrentWave; }

	UFUNCTION(BlueprintPure, Category = "Wave|State")
	int32 GetTotalWaveCount() const { return TotalWaveCount; }

	UFUNCTION(BlueprintPure, Category = "Wave|State")
	EGamePhase GetCurrentPhase() const { return CurrentPhase; }

	UFUNCTION(BlueprintPure, Category = "Wave|State")
	bool IsGameEnded() const { return bGameEnded; }
	
	//UFUNCTION(BlueprintPure, Category = "Wave|State")
	//bool IsGameFlowStarted() const { return bGameFlowStarted; }

	//UFUNCTION(BlueprintPure, Category = "Wave|State")
	//int32 GetAliveEnemyCount() const { return AliveEnemyCount; }

	

protected:
	// =========================================================
	// Internal
	// =========================================================

	void SetPhase(EGamePhase NewPhase);

	void StartIntermission(int32 NextDisplayWaveNumber, float Duration);

	void BeginPhaseRemainingTimer(float Duration);

	void ClearPhaseRemainingTimer();

	void SyncGameState();

	void ClearAllFlowTimers();

	bool IsLastWave(int32 DisplayWaveNumber) const;

	bool AreAllPlayersDead() const;
};
