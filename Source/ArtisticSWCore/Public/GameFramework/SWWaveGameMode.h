// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SWGamePhaseTypes.h"
#include "SWWaveGameMode.generated.h"

class ASWWaveGameState;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGameStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEnemySpawnCountdownStarted, int32, WaveIndex, float, Delay);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWaveStarted, int32, WaveIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWaveEnded, int32, WaveIndex, EWaveEndReason, EndReason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnIntermissionStarted, int32, NextWaveIndex, float, Duration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnIntermissionEnded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGameVictory);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGameDefeat);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPhaseChanged, EGamePhase, NewPhase);

/**
 * Wave 게임모드 규칙을 관리하는 게임모드 엑터
 */
UCLASS()
class ARTISTICSWCORE_API ASWWaveGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	/**
	 * 생성시, GameState를 설정
	*/
	ASWWaveGameMode();

protected:
	virtual void BeginPlay() override;

public:
	/** 게임 루프 시작 알림 */
	UPROPERTY(BlueprintAssignable, Category = "Wave Event")
	FOnGameStarted OnGameStarted;

	/** 몇번째 wave의 적이 몇 초 후 스폰될 지 알림, (int)WaveIndex 번호, (float)Delay 스폰 간격*/
	UPROPERTY(BlueprintAssignable, Category = "Wave Event")
	FOnEnemySpawnCountdownStarted OnEnemySpawnCountdownStarted;

	/** 웨이브 시작 알림, (int)WaveIndex 번호*/
	UPROPERTY(BlueprintAssignable, Category = "Wave Event")
	FOnWaveStarted OnWaveStarted;

	/** 웨이브 종료 알림, (int)WaveIndex 번호, (EWaveEndReason) EndReason 종료 원인*/
	UPROPERTY(BlueprintAssignable, Category = "Wave Event")
	FOnWaveEnded OnWaveEnded;

	/** 정비시간 시 알림, (int)NextWaveIndex 다음 웨이브 번호, (float) Duration 정비 시간 몇초 */
	UPROPERTY(BlueprintAssignable, Category = "Wave Event")
	FOnIntermissionStarted OnIntermissionStarted;

	/** 정비시간 종료 알림 */
	UPROPERTY(BlueprintAssignable, Category = "Wave Event")
	FOnIntermissionEnded OnIntermissionEnded;

	/** 최종 승리 알림 */
	UPROPERTY(BlueprintAssignable, Category = "Wave Event")
	FOnGameVictory OnGameVictory;

	/** 최종 실패 알림 */
	UPROPERTY(BlueprintAssignable, Category = "Wave Event")
	FOnGameDefeat OnGameDefeat;

	/** Phase 변경 알림, (EGamePhase) NewPhase 변경될 Phase */
	UPROPERTY(BlueprintAssignable, Category = "Wave Event")
	FOnPhaseChanged OnPhaseChanged;

protected:
	// 진행할 Wave 수
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wave Config")
	int32 MaxWaveCount = 3;

	// 웨이브 시작 후 적 스폰까지 딜레이
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wave Config")
	float FirstSpawnDelay = 5.f;

	// 한 웨이브당 시간 제한
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wave Config")
	float WaveTimeLimit = 60.f;

	// 정비 시간 길이
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wave Config")
	float IntermissionDuration = 15.f;

	// 패배 판정 규칙
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wave Config")
	EFailConditionType FailCondition = EFailConditionType::AnyPlayerDead;

protected:
	//=================================================================
	// Runtime 변수들
	//=================================================================
	
	// 현재 웨이브 번호
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wave Runtime")
	int32 CurrentWave = 0;

	// 살아있는 적의 수
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wave Runtime")
	int32 AliveEnemyCount = 0;

	// 현재 Phase
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wave Runtime")
	EGamePhase CurrentPhase = EGamePhase::None;

	// 게임 종료 여부
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wave Runtime")
	bool bGameEnded = false;

	// 웨이브 시작 후 적 최초 스폰 딜레이 타이머 핸들
	FTimerHandle EnemySpawnDelayHandle;
	// 한 웨이브의 제한 시간 타이머 핸들
	FTimerHandle WaveTimerHandle;
	// 정비 시간 타이머 핸들
	FTimerHandle IntermissionTimerHandle;
	// 현재 Phase의 나은시간을 주기적으로 갱신하는 타이머 핸들
	FTimerHandle PhaseTickHandle;

public:
	/**
	 * 게임을 시작하기 위해 Phase를 변경하고 적 스폰 시작 함수 호출
	 */
	UFUNCTION(BlueprintCallable, Category = "Wave Flow")
	void StartGameFlow();

	/**
	 * Phase를 WaitingEnemySpawn로 변경, FirstSpawnDelay 이후 적 스폰
	 */
	UFUNCTION(BlueprintCallable, Category = "Wave Flow")
	void StartEnemySpawnCountdown();

	/**
	 * Phase를 InWave로 변경, 웨이브 타이머 시작
	 */
	UFUNCTION(BlueprintCallable, Category = "Wave Flow")
	void StartWave();

	/**
	 * 웨이브 종료 관리, 최종 승리 또는 정비시간으로 넘어감
	 *  @param Reason 웨이브 종료 원인 (ex. AllEnemiesKilled)
	 */
	UFUNCTION(BlueprintCallable, Category = "Wave Flow")
	void EndWave(EWaveEndReason Reason);

	/**
	 * 정비시간 시작
	 */
	UFUNCTION(BlueprintCallable, Category = "Wave Flow")
	void StartIntermission();

	/**
	 * 정비시간 종료
	 */
	UFUNCTION(BlueprintCallable, Category = "Wave Flow")
	void EndIntermission();

	/**
	 * 최종 승리시 호출, Phase를 Victory로 설정
	 */
	UFUNCTION(BlueprintCallable, Category = "Wave Flow")
	void HandleVictory();

	/**
	 * 최종 실패시 호출, Phase를 Defeat로 설정
	 */
	UFUNCTION(BlueprintCallable, Category = "Wave Flow")
	void HandleDefeat();

	/**
	 * 웨이브 타이머 종료시 호출, TimeExpired로 EndWave() 호출
	 */
	UFUNCTION(BlueprintCallable, Category = "Wave Flow")
	void HandleWaveTimeExpired();

public:
	/**
	 * 적 스폰시 호출하여 ++AliveEnemyCount
	 */
	// TODO: 적 스포너에서 연결해야함
	UFUNCTION(BlueprintCallable, Category = "Wave Notify")
	void NotifyEnemySpawned();

	/**
	 * 적 처치시 호출하여 --AliveEnemyCount, 적의 수가 0이하가 되면, EndWave호출
	 */
	 // TODO: 적에서 죽을 때 호출
	UFUNCTION(BlueprintCallable, Category = "Wave Notify")
	void NotifyEnemyKilled();

	/**
	 * 플레이어가 죽을경우 호출
	 */
	 // TODO: 플레이어에서 죽을 때 호출
	UFUNCTION(BlueprintCallable, Category = "Wave Notify")
	void NotifyPlayerDead(AController* DeadPlayer);

protected:

	/**
	 *	Phase 설정,
	 *  @param NewPhase 새로 설정할 Phase (ex. InWave)
	 */
	void SetPhase(EGamePhase NewPhase);

	/**
	 *	GameState의 정보를 GameMode의 정보로 복사(서버->클라이언트)
	 */
	void SyncGameState();

	/**
	 *	현재 Phase에 맞게 남은 시간을 가져와 GameState에 저장
	 */
	void UpdateRemainingPhaseTime();

	/**
	 *	게임 종료 시 게임 흐름 관련 타이머 전부 제거
	 */
	void ClearAllFlowTimers();
	/**
	 *	플레이어 컨트롤러 순회하며 플레이어 사망 여부 확인
	 *  @return 전부 사망 여부 (전부 사망시 true)
	 */
	bool AreAllPlayersDead() const;

protected:
	/**
	 *	적 스폰 해야될 시점에 호출되는 함수, 블루프린트에서 스포너와 연결 후 스포너의 스폰 로직 실행
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Wave Spawn")
	void BP_SpawnEnemiesForWave(int32 WaveIndex);
};