// Fill out your copyright notice in the Description page of Project Settings.

#include "GameFramework/WaveGameMode.h"

#include "GameFramework/SWWaveGameState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"

AWaveGameMode::AWaveGameMode()
{
	GameStateClass = ASWWaveGameState::StaticClass();

	FailCondition = EFailConditionType::AnyPlayerDead;
	bAutoStartGameFlowWhenReady = true;
	bAutoRequestNextWaveAfterIntermission = true;
}

void AWaveGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 기존 버전처럼 BeginPlay에서 바로 StartGameFlow를 호출하지 않는다.
	// 2인 멀티에서는 플레이어 접속 / 역할 배정 / Ready 완료 이후 시작하는 것이 안전하다.
	SyncGameState();
}

void AWaveGameMode::HandleAllPlayersReady()
{
	Super::HandleAllPlayersReady();

	if (!bAutoStartGameFlowWhenReady)
	{
		return;
	}

	// WaveSpawnManager가 아직 BeginPlay/Binding/DataReady를 끝내지 않았다면
	// 여기서 시작 요청을 보내지 않고 NotifyWaveDataReady에서 다시 시도한다.
	if (TotalWaveCount <= 0)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[AWaveGameMode] Waiting for WaveData before starting game flow.")
		);
		return;
	}

	StartGameFlow();
}

void AWaveGameMode::StartGameFlow()
{
	if (!HasAuthority())
	{
		return;
	}

	if (bGameEnded || bGameFlowStarted)
	{
		return;
	}

	if (TotalWaveCount <= 0)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[AWaveGameMode] StartGameFlow blocked: WaveData is not ready yet.")
		);
		return;
	}

	bGameFlowStarted = true;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[AWaveGameMode] Game flow started. TotalWaveCount=%d"),
		TotalWaveCount
	);

	SetPhase(EGamePhase::GameStarting);
	OnGameStarted.Broadcast();

	RequestStartNextWave();
}

void AWaveGameMode::RequestStartNextWave()
{
	if (!HasAuthority())
	{
		return;
	}

	if (bGameEnded)
	{
		return;
	}

	const int32 NextDisplayWaveNumber = CurrentWave + 1;

	if (TotalWaveCount > 0 && NextDisplayWaveNumber > TotalWaveCount)
	{
		HandleVictory();
		return;
	}

	// 여기서 GameMode는 PreWaveDelay 값을 모른다.
	// 단지 "다음 Wave를 시작해라"라는 요청만 보낸다.
	// 실제 PreWaveDelay / SpawnGroup 실행은 AWaveSpawnManager가 WaveDataAsset을 읽어 처리한다.
	SetPhase(EGamePhase::WaitingEnemySpawn);

	OnWaveStartRequested.Broadcast(NextDisplayWaveNumber);

	SyncGameState();
}

void AWaveGameMode::RequestStopCurrentWave(EWaveEndReason EndReason)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bGameEnded)
	{
		return;
	}

	// GameMode는 Enemy를 직접 정리하지 않는다.
	// WaveSpawnManager에게 현재 Wave 중단을 요청한다.
	OnWaveStopRequested.Broadcast(CurrentWave, EndReason);
}

void AWaveGameMode::EndIntermission()
{
	if (!HasAuthority())
	{
		return;
	}

	if (bGameEnded)
	{
		return;
	}

	if (CurrentPhase != EGamePhase::Intermission)
	{
		return;
	}

	ClearPhaseRemainingTimer();

	OnIntermissionEnded.Broadcast();

	if (bAutoRequestNextWaveAfterIntermission)
	{
		RequestStartNextWave();
	}
}

void AWaveGameMode::HandleVictory()
{
	if (!HasAuthority())
	{
		return;
	}

	if (bGameEnded)
	{
		return;
	}

	bGameEnded = true;

	ClearAllFlowTimers();

	SetPhase(EGamePhase::Victory);
	OnGameVictory.Broadcast();

	SyncGameState();
}

void AWaveGameMode::HandleDefeat()
{
	if (!HasAuthority())
	{
		return;
	}

	if (bGameEnded)
	{
		return;
	}

	// 현재 WaveSystem이 동작 중일 수 있으므로 중단 요청을 먼저 보낸다.
	// 현재 EWaveEndReason에는 Defeat / Cancelled가 없기 때문에 TimeExpired를 임시 중단 사유로 사용한다.
	// 추후 EWaveEndReason에 CancelledByGameMode 또는 Defeat를 추가하는 것이 더 명확하다.
	if (CurrentPhase == EGamePhase::WaitingEnemySpawn ||
		CurrentPhase == EGamePhase::InWave ||
		CurrentPhase == EGamePhase::Intermission)
	{
		OnWaveStopRequested.Broadcast(CurrentWave, EWaveEndReason::TimeExpired);
	}

	bGameEnded = true;

	ClearAllFlowTimers();

	SetPhase(EGamePhase::Defeat);
	OnGameDefeat.Broadcast();

	SyncGameState();
}

void AWaveGameMode::NotifyPlayerDead(AController* DeadPlayer)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bGameEnded)
	{
		return;
	}

	switch (FailCondition)
	{
	case EFailConditionType::AnyPlayerDead:
		HandleDefeat();
		return;

	case EFailConditionType::AllPlayersDead:
		if (AreAllPlayersDead())
		{
			HandleDefeat();
		}
		return;

	default:
		return;
	}
}

void AWaveGameMode::NotifyWaveDataReady_Implementation(int32 InTotalWaveCount)
{
	if (!HasAuthority())
	{
		return;
	}

	if (InTotalWaveCount <= 0)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ASWWaveGameMode] NotifyWaveDataReady ignored. Invalid TotalWaveCount=%d"),
			InTotalWaveCount
		);
		return;
	}

	TotalWaveCount = InTotalWaveCount;

	SyncGameState();

	// 플레이어 준비가 먼저 끝난 경우,
	// WaveSpawnManager가 준비 완료된 이 시점에서 게임 흐름을 다시 시작한다.
	if (bAutoStartGameFlowWhenReady &&
		AreAllPlayersReady() &&
		!bGameFlowStarted &&
		!bGameEnded)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[AWaveGameMode] Starting game flow after WaveData became ready.")
		);

		StartGameFlow();
	}
}

void AWaveGameMode::NotifyWaveCountdownStarted_Implementation(
	int32 DisplayWaveNumber,
	float CountdownDuration
)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bGameEnded)
	{
		return;
	}

	CurrentWave = DisplayWaveNumber;
	AliveEnemyCount = 0;

	SetPhase(EGamePhase::WaitingEnemySpawn);

	BeginPhaseRemainingTimer(CountdownDuration);

	OnEnemySpawnCountdownStarted.Broadcast(DisplayWaveNumber, CountdownDuration);

	SyncGameState();
}

void AWaveGameMode::NotifyWaveRuntimeStarted_Implementation(
	int32 WaveArrayIndex,
	int32 DisplayWaveNumber,
	float WaveTimeLimit
)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bGameEnded)
	{
		return;
	}

	CurrentWave = DisplayWaveNumber;
	AliveEnemyCount = 0;

	SetPhase(EGamePhase::InWave);

	// WaveTimeLimit은 WaveDataAsset에서 온 값이다.
	// GameMode가 이 값을 Config로 소유하지는 않는다.
	// 여기서는 UI 표시용 RemainingPhaseTime 계산에만 사용한다.
	BeginPhaseRemainingTimer(WaveTimeLimit);

	OnWaveRuntimeStarted.Broadcast(WaveArrayIndex, DisplayWaveNumber, WaveTimeLimit);

	SyncGameState();
}

void AWaveGameMode::NotifyWaveEnemyCountChanged_Implementation(int32 NewAliveEnemyCount)
{
	if (!HasAuthority())
	{
		return;
	}

	AliveEnemyCount = FMath::Max(NewAliveEnemyCount, 0);

	SyncGameState();
}

void AWaveGameMode::NotifyWaveCompleted_Implementation(
	int32 WaveArrayIndex,
	int32 DisplayWaveNumber,
	bool bSuccess,
	float PostWaveDelay
)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bGameEnded)
	{
		return;
	}

	if (DisplayWaveNumber != CurrentWave)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ASWWaveGameMode] NotifyWaveCompleted wave mismatch. CurrentWave=%d, ReportedDisplayWave=%d, WaveArrayIndex=%d"),
			CurrentWave,
			DisplayWaveNumber,
			WaveArrayIndex
		);
	}

	AliveEnemyCount = 0;
	ClearPhaseRemainingTimer();

	SetPhase(EGamePhase::WaveEnding);

	OnWaveEnded.Broadcast(DisplayWaveNumber, EWaveEndReason::AllEnemiesKilled);

	SyncGameState();

	if (!bSuccess)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ASWWaveGameMode] Wave completed with bSuccess=false. DisplayWaveNumber=%d"),
			DisplayWaveNumber
		);

		HandleDefeat();
		return;
	}

	if (IsLastWave(DisplayWaveNumber))
	{
		HandleVictory();
		return;
	}

	StartIntermission(DisplayWaveNumber + 1, PostWaveDelay);
}

void AWaveGameMode::NotifyWaveStopped_Implementation(
	int32 WaveArrayIndex,
	int32 DisplayWaveNumber,
	EWaveEndReason EndReason,
	float PostWaveDelay
)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bGameEnded)
	{
		return;
	}

	ClearPhaseRemainingTimer();

	SetPhase(EGamePhase::WaveEnding);

	OnWaveEnded.Broadcast(DisplayWaveNumber, EndReason);

	SyncGameState();

	// 기존 SWWaveGameMode의 동작과 맞추기 위해 TimeExpired도 곧바로 Defeat로 보지 않는다.
	// 즉, 특수 종료 후에도 마지막 Wave면 Victory, 아니면 Intermission으로 간다.
	// 게임 규칙상 TimeExpired를 패배로 보려면 여기서 HandleDefeat()로 바꾸면 된다.
	if (IsLastWave(DisplayWaveNumber))
	{
		HandleVictory();
		return;
	}

	StartIntermission(DisplayWaveNumber + 1, PostWaveDelay);
}

void AWaveGameMode::SetPhase(EGamePhase NewPhase)
{
	if (CurrentPhase == NewPhase)
	{
		return;
	}

	CurrentPhase = NewPhase;

	SyncGameState();

	OnPhaseChanged.Broadcast(NewPhase);
}

void AWaveGameMode::StartIntermission(int32 NextDisplayWaveNumber, float Duration)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bGameEnded)
	{
		return;
	}

	SetPhase(EGamePhase::Intermission);

	BeginPhaseRemainingTimer(Duration);

	OnIntermissionStarted.Broadcast(NextDisplayWaveNumber, Duration);

	GetWorldTimerManager().ClearTimer(IntermissionTimerHandle);

	if (Duration <= 0.f)
	{
		EndIntermission();
		return;
	}

	GetWorldTimerManager().SetTimer(
		IntermissionTimerHandle,
		this,
		&AWaveGameMode::EndIntermission,
		Duration,
		false
	);
}

void AWaveGameMode::BeginPhaseRemainingTimer(float Duration)
{
	if (!HasAuthority())
	{
		return;
	}

	if (ASWWaveGameState* GS = GetGameState<ASWWaveGameState>())
	{
		GS->SetPhaseCountdownFromDuration(Duration);
	}
}

void AWaveGameMode::ClearPhaseRemainingTimer()
{
	if (!HasAuthority())
	{
		return;
	}

	if (ASWWaveGameState* GS = GetGameState<ASWWaveGameState>())
	{
		GS->ClearPhaseCountdown();
	}
}

void AWaveGameMode::SyncGameState()
{
	ASWWaveGameState* GS = GetGameState<ASWWaveGameState>();
	if (!GS)
	{
		return;
	}

	GS->SetCurrentPhase(CurrentPhase);
	GS->SetCurrentWave(CurrentWave);

	// 기존 ASWWaveGameState의 변수명이 MaxWaveCount이므로,
	// GameMode의 TotalWaveCount를 여기에 넣는다.
	GS->SetMaxWaveCount(TotalWaveCount);

	GS->SetAliveEnemyCount(AliveEnemyCount);
}

void AWaveGameMode::ClearAllFlowTimers()
{
	GetWorldTimerManager().ClearTimer(IntermissionTimerHandle);

	ClearPhaseRemainingTimer();
}

bool AWaveGameMode::IsLastWave(int32 DisplayWaveNumber) const
{
	// TotalWaveCount가 아직 보고되지 않았으면 마지막 Wave 판정을 하지 않는다.
	if (TotalWaveCount <= 0)
	{
		return false;
	}

	return DisplayWaveNumber >= TotalWaveCount;
}

bool AWaveGameMode::AreAllPlayersDead() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}

		APawn* Pawn = PC->GetPawn();

		if (Pawn && !Pawn->IsActorBeingDestroyed())
		{
			return false;
		}
	}

	return true;
}

void AWaveGameMode::HandleAllPlayersDeathFinished()
{
	if (!HasAuthority()) return;
	HandleDefeat(); // Replicated Defeat phase + OnGameDefeat are the future defeat-screen hook.
	const UWorld* World = GetWorld();
	if (!World) return;
	UGameplayStatics::OpenLevel(this, FName(*World->GetMapName().RightChop(World->StreamingLevelsPrefix.Len())));
}
