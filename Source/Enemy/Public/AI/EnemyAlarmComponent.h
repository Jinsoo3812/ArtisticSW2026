#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DeckAI/DeckWalkTypes.h"
#include "EnemyAlarmComponent.generated.h"

class AEnemyShip;
class USoundBase;
struct FAIStimulus;

/** Per-enemy session and immutable hearing snapshot. No target is assigned by receiving an alarm. */
UCLASS(ClassGroup = (Enemy))
class ENEMY_API UEnemyAlarmComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UEnemyAlarmComponent();
	void SetCombatActive(bool bActive);
	void ResetForReuse();
	bool IsAlarmPending() const { return bCombatActive && BroadcastSession != CombatSession; }
	bool Broadcast(AActor* ObservedPlayer, float Range, float Loudness, float Cooldown, USoundBase* Sound);
	bool Receive(const UEnemyAlarmComponent& Sender, const FAIStimulus& Stimulus, FVector& OutInvestigation);
	bool GetInvestigationWorld(FVector& Out) const;
	void CaptureInvestigationWorld(const FVector& Point);
	void ClearInvestigation();
	static bool IsAlarmTag(FName Tag);
private:
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastAlarmSound(USoundBase* Sound, FVector_NetQuantize Location);
	uint64 CombatSession = 0;
	uint64 BroadcastSession = 0;
	bool bCombatActive = false;
	double LastBroadcastTime = -DBL_MAX;
	double EmittedTime = -DBL_MAX;
	FName EmittedTag;
	TWeakObjectPtr<AActor> EmittedPlayer;
	TWeakObjectPtr<AEnemyShip> EmittedShip;
	FDeckWalkLocation EmittedFloor;
	FVector EmittedWorld = FVector::ZeroVector;
	TWeakObjectPtr<AEnemyShip> InvestigationShip;
	FDeckWalkLocation InvestigationFloor;
	TWeakObjectPtr<AActor> LastHeardPlayer;
	double LastHeardTime = -DBL_MAX;
	TMap<FName, double> ReceivedTags;
};
