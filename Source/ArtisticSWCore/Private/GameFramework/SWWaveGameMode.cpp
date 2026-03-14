// Fill out your copyright notice in the Description page of Project Settings.


#include "GameFramework/SWWaveGameMode.h"
#include "GameFramework/SWWaveGameState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

ASWWaveGameMode::ASWWaveGameMode()
{
	GameStateClass = ASWWaveGameState::StaticClass();
}

void ASWWaveGameMode::BeginPlay()
{
	Super::BeginPlay();

	SyncGameState(); // GameMode의 초기값을 GameState에 반영

	StartGameFlow();
}

void ASWWaveGameMode::StartGameFlow()
{
	if (bGameEnded)
	{
		return;
	}

	if (CurrentPhase != EGamePhase::None)
	{
		return;
	}

	SetPhase(EGamePhase::GameStarting);
	OnGameStarted.Broadcast();

	StartEnemySpawnCountdown();
}

void ASWWaveGameMode::StartEnemySpawnCountdown()
{
	if (bGameEnded)
	{
		return;
	}

	SetPhase(EGamePhase::WaitingEnemySpawn);
	OnEnemySpawnCountdownStarted.Broadcast(CurrentWave + 1, FirstSpawnDelay);

	GetWorldTimerManager().ClearTimer(EnemySpawnDelayHandle);
	GetWorldTimerManager().SetTimer(
		EnemySpawnDelayHandle,
		this,
		&ASWWaveGameMode::StartWave,
		FirstSpawnDelay,
		false
	);
}

void ASWWaveGameMode::StartWave()
{
	if (bGameEnded)
	{
		return;
	}

	++CurrentWave;
	AliveEnemyCount = 0;

	SetPhase(EGamePhase::InWave);
	OnWaveStarted.Broadcast(CurrentWave);

	// 스포너에서 적 생성 후 NotifyEnemySpawned() 호출
	BP_SpawnEnemiesForWave(CurrentWave);

	GetWorldTimerManager().ClearTimer(WaveTimerHandle);
	GetWorldTimerManager().SetTimer(
		WaveTimerHandle,
		this,
		&ASWWaveGameMode::HandleWaveTimeExpired,
		WaveTimeLimit,
		false
	);

	SyncGameState();
}

void ASWWaveGameMode::HandleWaveTimeExpired()
{
	if (bGameEnded)
	{
		return;
	}

	if (CurrentPhase != EGamePhase::InWave)
	{
		return;
	}

	EndWave(EWaveEndReason::TimeExpired);
}

void ASWWaveGameMode::EndWave(EWaveEndReason Reason)
{
	if (bGameEnded)
	{
		return;
	}

	if (CurrentPhase != EGamePhase::InWave)
	{
		return;
	}

	SetPhase(EGamePhase::WaveEnding);

	GetWorldTimerManager().ClearTimer(WaveTimerHandle);

	OnWaveEnded.Broadcast(CurrentWave, Reason);

	if (CurrentWave >= MaxWaveCount)
	{
		HandleVictory();
		return;
	}

	StartIntermission();
}

void ASWWaveGameMode::StartIntermission()
{
	if (bGameEnded)
	{
		return;
	}

	SetPhase(EGamePhase::Intermission);
	OnIntermissionStarted.Broadcast(CurrentWave + 1, IntermissionDuration);

	GetWorldTimerManager().ClearTimer(IntermissionTimerHandle);
	GetWorldTimerManager().SetTimer(
		IntermissionTimerHandle,
		this,
		&ASWWaveGameMode::EndIntermission,
		IntermissionDuration,
		false
	);
}

void ASWWaveGameMode::EndIntermission()
{
	if (bGameEnded)
	{
		return;
	}

	if (CurrentPhase != EGamePhase::Intermission)
	{
		return;
	}

	OnIntermissionEnded.Broadcast();
	StartEnemySpawnCountdown();
}

void ASWWaveGameMode::HandleVictory()
{
	if (bGameEnded)
	{
		return;
	}

	bGameEnded = true;
	SetPhase(EGamePhase::Victory);
	ClearAllFlowTimers();
	OnGameVictory.Broadcast();
}

void ASWWaveGameMode::HandleDefeat()
{
	if (bGameEnded)
	{
		return;
	}

	bGameEnded = true;
	SetPhase(EGamePhase::Defeat);
	ClearAllFlowTimers();
	OnGameDefeat.Broadcast();
}

void ASWWaveGameMode::NotifyEnemySpawned()
{
	if (bGameEnded)
	{
		return;
	}

	++AliveEnemyCount;
	SyncGameState();
}

void ASWWaveGameMode::NotifyEnemyKilled()
{
	if (bGameEnded)
	{
		return;
	}

	if (CurrentPhase != EGamePhase::InWave)
	{
		return;
	}

	AliveEnemyCount = FMath::Max(AliveEnemyCount - 1, 0);
	SyncGameState();

	if (AliveEnemyCount <= 0)
	{
		EndWave(EWaveEndReason::AllEnemiesKilled);
	}
}

void ASWWaveGameMode::NotifyPlayerDead(AController* DeadPlayer)
{
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

void ASWWaveGameMode::SetPhase(EGamePhase NewPhase)
{
	if (CurrentPhase == NewPhase)
	{
		return;
	}

	CurrentPhase = NewPhase;
	SyncGameState();
	OnPhaseChanged.Broadcast(NewPhase);

	GetWorldTimerManager().ClearTimer(PhaseTickHandle);
	GetWorldTimerManager().SetTimer(
		PhaseTickHandle,
		this,
		&ASWWaveGameMode::UpdateRemainingPhaseTime,
		0.1f,
		true
	);

	UpdateRemainingPhaseTime();


	//================
	// for debugging
	//================
	ASWWaveGameState* GS = GetGameState<ASWWaveGameState>();
	if (!GS) return;

	GS->CurrentPhase = NewPhase;

	// 서버 자신의 화면에서도 바로 보이게
	GS->PrintCurrentPhase();
	//================
	// end of for debugging
	//================

}

void ASWWaveGameMode::SyncGameState()
{
	ASWWaveGameState* GS = GetGameState<ASWWaveGameState>();
	if (!GS)
	{
		return;
	}

	GS->SetCurrentPhase(CurrentPhase);
	GS->SetCurrentWave(CurrentWave);
	GS->SetMaxWaveCount(MaxWaveCount);
	GS->SetAliveEnemyCount(AliveEnemyCount);
}

void ASWWaveGameMode::UpdateRemainingPhaseTime()
{
	ASWWaveGameState* GS = GetGameState<ASWWaveGameState>();
	if (!GS)
	{
		return;
	}

	float Remaining = 0.f;

	switch (CurrentPhase)
	{
	case EGamePhase::WaitingEnemySpawn:
		Remaining = GetWorldTimerManager().GetTimerRemaining(EnemySpawnDelayHandle);
		break;

	case EGamePhase::InWave:
		Remaining = GetWorldTimerManager().GetTimerRemaining(WaveTimerHandle);
		break;

	case EGamePhase::Intermission:
		Remaining = GetWorldTimerManager().GetTimerRemaining(IntermissionTimerHandle);
		break;

	default:
		Remaining = 0.f;
		break;
	}

	GS->SetRemainingPhaseTime(FMath::Max(Remaining, 0.f));
}

void ASWWaveGameMode::ClearAllFlowTimers()
{
	GetWorldTimerManager().ClearTimer(EnemySpawnDelayHandle);
	GetWorldTimerManager().ClearTimer(WaveTimerHandle);
	GetWorldTimerManager().ClearTimer(IntermissionTimerHandle);
	GetWorldTimerManager().ClearTimer(PhaseTickHandle);

	if (ASWWaveGameState* GS = GetGameState<ASWWaveGameState>())
	{
		GS->SetRemainingPhaseTime(0.f);
	}
}

bool ASWWaveGameMode::AreAllPlayersDead() const
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
