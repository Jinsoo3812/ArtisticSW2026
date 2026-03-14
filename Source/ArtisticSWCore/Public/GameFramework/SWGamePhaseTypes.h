// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "SWGamePhaseTypes.generated.h"

/**
 * 현재 게임의 단계를 나타내는 상태값들을 모아놓은 Enum
 */
UENUM(BlueprintType)
enum class EGamePhase : uint8
{
	// 게임 시작 전
	None                UMETA(DisplayName = "None"),
	// 게임 시작 직후
	GameStarting        UMETA(DisplayName = "GameStarting"),
	// 적 스폰 전 대기 시간
	WaitingEnemySpawn   UMETA(DisplayName = "WaitingEnemySpawn"),
	// 웨이브 진행중
	InWave              UMETA(DisplayName = "InWave"),
	// 웨이브 종료 (보상 정산, 결과 UI 처리 시)
	WaveEnding          UMETA(DisplayName = "WaveEnding"),
	// 중간 정비 시간
	Intermission        UMETA(DisplayName = "Intermission"),
	// 최종 승리
	Victory             UMETA(DisplayName = "Victory"),
	// 최종 실패
	Defeat              UMETA(DisplayName = "Defeat")
};

/**
 * 웨이브 종료 원인
 */
UENUM(BlueprintType)
enum class EWaveEndReason : uint8
{
	// 모든 적이 다 죽었을 때
	AllEnemiesKilled    UMETA(DisplayName = "AllEnemiesKilled"),
	// 사전 설정한 시간이 다 지났을 때
	TimeExpired         UMETA(DisplayName = "TimeExpired")
};

/**
 * 최종 실패 원인, 패배 조건
 */
UENUM(BlueprintType)
enum class EFailConditionType : uint8
{
	// 한 플레이어가 죽었을 때 
	AnyPlayerDead       UMETA(DisplayName = "AnyPlayerDead"),
	// 모든 플레이어가 죽었을 때
	AllPlayersDead      UMETA(DisplayName = "AllPlayersDead")
};
