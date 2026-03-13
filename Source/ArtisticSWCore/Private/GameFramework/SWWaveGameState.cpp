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
	DOREPLIFETIME(ASWWaveGameState, RemainingPhaseTime);
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
