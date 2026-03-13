// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "SWGamePhaseTypes.h"
#include "SWWaveGameState.generated.h"

/**
 * 클라이언트도 알아야 하는(플레이어UI에 전달 되어야 하는) 현재 게임 정보를 저장
 */
UCLASS()
class ARTISTICSWCORE_API ASWWaveGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	/**
	* GameState 객체를 리플리케이션 가능 상태로 설정함
	*/
	ASWWaveGameState();

	/**
	* Replicated 지정자를 붙인 변수를 리플리케이션한다고 등록하는 함수
	* 함수내 매크로에서 실제로 등록해 줘야함, 등록된 변수들의 서버 값이 바뀌면 클라이언트로 전송
	* @param OutLifetimeProps
	*/
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	// 현재 Phase (SWGamePhaseTypes - EGamePhase)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Wave State")
	EGamePhase CurrentPhase = EGamePhase::None;

	// 현재 Wave (1, 2, 3 ... )
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Wave State")
	int32 CurrentWave = 0;

	// 최대 Wave
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Wave State")
	int32 MaxWaveCount = 3;

	// 현재 살아 있는 적의 수
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Wave State")
	int32 AliveEnemyCount = 0;

	// 잔여 Phase 시간
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Wave State")
	float RemainingPhaseTime = 0.f;

public:
	/**
	* Phase 전환
	* @param NewPhase 변경할 새로운 Phase (ex. EGamePhase::Intermission)
	*/
	void SetCurrentPhase(EGamePhase NewPhase) { CurrentPhase = NewPhase; }

	/**
	* 현재 Wave 번호 저장
	* @param NewWave 변경할 새로운 Phase (ex. 1, 2, 3 ... )
	*/
	void SetCurrentWave(int32 NewWave) { CurrentWave = NewWave; }

	/**
	* 최대 Wave 저장
	* @param MaxWave 최대 Wave 수 (ex. 1, 2, 3...)
	*/
	void SetMaxWaveCount(int32 NewMaxWaveCount) { MaxWaveCount = NewMaxWaveCount; }

	/**
	* 현재 살아있는 적의 수 저장
	* @param NewAliveEnemyCount 살아있는 적의 수 (ex. 1, 2, 3 ...)
	*/
	void SetAliveEnemyCount(int32 NewAliveEnemyCount) { AliveEnemyCount = NewAliveEnemyCount; }

	/**
	* 현재 Phase의 남은 시간 저장
	* @param NewTime 남은 시간 (ex. 30, 60, 90... )
	*/
	void SetRemainingPhaseTime(float NewTime) { RemainingPhaseTime = NewTime; }


	//================
	// for debugging
	//================
	UFUNCTION()
	void OnRep_CurrentPhase();

	void PrintCurrentPhase() const;
};
