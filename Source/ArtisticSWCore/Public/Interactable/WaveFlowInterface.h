// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameFramework/SWGamePhaseTypes.h"

#include "WaveFlowInterface.generated.h"

/**
 * WaveSystem이 GameMode에 Wave 진행 상황을 보고하기 위한 Interface.
 *
 * 핵심 의도:
 * - Core의 GameMode는 Enemy 모듈의 AWaveSpawnManager를 직접 모른다.
 * - Enemy 모듈의 AWaveSpawnManager는 Core의 이 Interface만 알고 보고한다.
 * - Wave 데이터의 실제 소스는 Enemy 모듈의 WaveDataAsset이다.
 */

UINTERFACE()
class UWaveFlowInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class ARTISTICSWCORE_API IWaveFlowInterface
{
	GENERATED_BODY()
	
public:
	/**
	 * WaveDataAsset 검증이 끝났고, 총 Wave 수를 알 수 있을 때 호출.
	 * 호출 주체:
	 * - AWaveSpawnManager
	 * 수신 주체:
	 * - ASWWaveGameMode
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Wave|Flow")
	void NotifyWaveDataReady(int32 InTotalWaveCount);

	/**
	 * 특정 Wave의 PreWaveDelay가 시작되었을 때 호출.
	 * DisplayWaveNumber:
	 * - 플레이어/UI 기준 Wave 번호. 보통 1, 2, 3...
	 * CountdownDuration:
	 * - WaveDataAsset의 FWaveDefinition::PreWaveDelay 값.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Wave|Flow")
	void NotifyWaveCountdownStarted(int32 DisplayWaveNumber, float CountdownDuration);

	/**
	 * PreWaveDelay가 끝나고 실제 Wave SpawnGroup 실행이 시작되었을 때 호출.
	 * WaveArrayIndex:
	 * - WaveDataAsset 내부 배열 인덱스.
	 * DisplayWaveNumber:
	 * - 플레이어/UI 기준 Wave 번호.
	 * WaveTimeLimit:
	 * - WaveDataAsset의 Wave별 제한 시간.
	 * - 0 이하이면 시간 제한 없음으로 취급.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Wave|Flow")
	void NotifyWaveRuntimeStarted(int32 WaveArrayIndex, int32 DisplayWaveNumber, float WaveTimeLimit);

	/**
	 * WaveSpawnManager가 계산한 살아있는 Enemy 수가 바뀔 때 호출.
	 * AliveEnemyCount의 실제 계산 책임은 AWaveSpawnManager가 가진다.
	 * GameMode는 이 값을 GameState에 복제하기만 한다.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Wave|Flow")
	void NotifyWaveEnemyCountChanged(int32 NewAliveEnemyCount);

	/**
	 * 현재 Wave가 정상 완료되었을 때 호출.
	 * PostWaveDelay:
	 * - WaveDataAsset의 현재 Wave 종료 후 다음 Wave까지의 대기 시간.
	 * - GameMode는 이 값을 이용해서 Intermission Phase를 처리한다.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Wave|Flow")
	void NotifyWaveCompleted(
		int32 WaveArrayIndex,
		int32 DisplayWaveNumber,
		bool bSuccess,
		float PostWaveDelay
	);

	/**
	 * 현재 Wave가 강제 중단되었거나 시간 초과 등으로 종료되었을 때 호출.
	 * TimeExpired, 강제 중단, 데이터 실패 같은 비정상/특수 종료 보고용.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Wave|Flow")
	void NotifyWaveStopped(
		int32 WaveArrayIndex,
		int32 DisplayWaveNumber,
		EWaveEndReason EndReason,
		float PostWaveDelay
	);
};
