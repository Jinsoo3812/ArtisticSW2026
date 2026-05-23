// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "SWGamePhaseTypes.h"
#include "SWWaveGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPhaseCountdownChanged);

USTRUCT(BlueprintType)
struct FReplicatedPhaseCountdown
{
	GENERATED_BODY()

public:
	/**
	 * 현재 Phase가 제한 시간 또는 표시용 카운트다운을 가지고 있는지 여부.
	 *
	 * false:
	 * - Waiting 상태처럼 카운트다운이 없는 경우
	 * - Victory / Defeat처럼 시간이 필요 없는 경우
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Wave State|Countdown")
	bool bIsActive = false;

	/**
	 * 이 Phase가 종료되는 서버 기준 시각.
	 *
	 * 남은 시간은 다음 방식으로 계산한다.
	 * EndServerWorldTimeSeconds - GameState.GetServerWorldTimeSeconds()
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Wave State|Countdown")
	double EndServerWorldTimeSeconds = 0.0;
};

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
	UPROPERTY(ReplicatedUsing = OnRep_CurrentPhase, BlueprintReadOnly, Category = "Wave State")
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

	// 현재 Phase의 종료 서버 시각과 활성 여부
	UPROPERTY(ReplicatedUsing = OnRep_PhaseCountdown,BlueprintReadOnly,Category = "Wave State|Countdown")
	FReplicatedPhaseCountdown PhaseCountdown;

	// Widget BP가 카운트다운 시작/종료 시 UI 표시 상태를 갱신할 때 사용할 수 있다.
	UPROPERTY(BlueprintAssignable, Category = "Wave State|Countdown")
	FOnPhaseCountdownChanged OnPhaseCountdownChanged;
	
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
 * 서버에서 새로운 Phase 카운트다운을 시작한다.
 *
 * @param Duration 현재 시점부터 종료까지의 시간.
 *                 0 이하이면 카운트다운을 비활성화한다.
 */
	void SetPhaseCountdownFromDuration(float Duration);

	/**
	 * 서버에서 현재 Phase 카운트다운을 제거한다.
	 */
	void ClearPhaseCountdown();

public:
	// Getters
	
	/**
	 * 클라이언트 또는 서버에서 현재 남은 시간을 계산한다.
	 * 서버가 매 프레임 값을 복제하지 않아도 Widget이 이 함수를 호출해 표시 가능하다.
	 */
	UFUNCTION(BlueprintPure, Category = "Wave State|Countdown")
	float GetRemainingPhaseTime() const;

	/**
	 * 현재 표시할 Phase 카운트다운이 존재하는지 반환한다.
	 */
	UFUNCTION(BlueprintPure, Category = "Wave State|Countdown")
	bool HasPhaseCountdown() const
	{
		return PhaseCountdown.bIsActive;
	}

	//================
	// for debugging
	//================
	UFUNCTION()
	void OnRep_CurrentPhase();

	void PrintCurrentPhase() const;

	UFUNCTION()
	void OnRep_PhaseCountdown();

private:
	void BroadcastPhaseCountdownChanged();
};
