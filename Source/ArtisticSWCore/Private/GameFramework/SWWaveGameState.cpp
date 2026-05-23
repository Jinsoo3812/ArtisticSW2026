// Fill out your copyright notice in the Description page of Project Settings.


#include "GameFramework/SWWaveGameState.h"
#include "Net/UnrealNetwork.h"
#include "Engine/Engine.h"

ASWWaveGameState::ASWWaveGameState()
{
	bReplicates = true;
}

void ASWWaveGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASWWaveGameState, CurrentPhase);
	DOREPLIFETIME(ASWWaveGameState, CurrentWave);
	DOREPLIFETIME(ASWWaveGameState, MaxWaveCount);
	DOREPLIFETIME(ASWWaveGameState, AliveEnemyCount);
	DOREPLIFETIME(ASWWaveGameState, PhaseCountdown);
}

void ASWWaveGameState::SetPhaseCountdownFromDuration(float Duration)
{
	if (!HasAuthority())
	{
		return;
	}

	if (Duration <= 0.f)
	{
		ClearPhaseCountdown();
		return;
	}

	PhaseCountdown.bIsActive = true;
	PhaseCountdown.EndServerWorldTimeSeconds =
		GetServerWorldTimeSeconds() + static_cast<double>(Duration);

	// Phase 변경 직후 UI에 빠르게 전달되도록 복제를 촉진한다.
	ForceNetUpdate();

	// Listen Server 화면의 Widget도 즉시 갱신할 수 있도록 서버 로컬 이벤트를 발생시킨다.
	BroadcastPhaseCountdownChanged();
}

void ASWWaveGameState::ClearPhaseCountdown()
{
	if (!HasAuthority())
	{
		return;
	}

	if (!PhaseCountdown.bIsActive &&
		PhaseCountdown.EndServerWorldTimeSeconds <= 0.0)
	{
		return;
	}

	PhaseCountdown.bIsActive = false;
	PhaseCountdown.EndServerWorldTimeSeconds = 0.0;

	ForceNetUpdate();
	BroadcastPhaseCountdownChanged();
}

float ASWWaveGameState::GetRemainingPhaseTime() const
{
	if (!PhaseCountdown.bIsActive)
	{
		return 0.f;
	}

	const double RemainingTime =
		PhaseCountdown.EndServerWorldTimeSeconds -
		GetServerWorldTimeSeconds();

	return static_cast<float>(FMath::Max(RemainingTime, 0.0));
}

//================
// for debugging
//================
void ASWWaveGameState::OnRep_CurrentPhase()
{
	PrintCurrentPhase();
}

void ASWWaveGameState::PrintCurrentPhase() const
{
	if (!GEngine) return;

	const UEnum* EnumPtr = StaticEnum<EGamePhase>();
	const FString PhaseText = EnumPtr
		? EnumPtr->GetNameStringByValue((int64)CurrentPhase)
		: TEXT("Unknown");

	GEngine->AddOnScreenDebugMessage(
		999,
		2.0f,
		FColor::Yellow,
		FString::Printf(TEXT("Phase Changed -> %s"), *PhaseText)
	);
}

void ASWWaveGameState::OnRep_PhaseCountdown()
{
	BroadcastPhaseCountdownChanged();
}

void ASWWaveGameState::BroadcastPhaseCountdownChanged()
{
	OnPhaseCountdownChanged.Broadcast();
}