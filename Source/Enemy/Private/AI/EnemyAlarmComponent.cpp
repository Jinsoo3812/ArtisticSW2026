#include "AI/EnemyAlarmComponent.h"

#include "BaseEnemy.h"
#include "Components/BaseHealthComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckCombatTargetResolverComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Perception/AIPerceptionTypes.h"
#include "Perception/AISense_Hearing.h"
#include "ShipAI/EnemyShip.h"

namespace { const FString AlarmPrefix(TEXT("Enemy.Alarm.PlayerSighted.")); }
UEnemyAlarmComponent::UEnemyAlarmComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Only optional sound RPCs replicate. All decision and snapshot data remain server-only.
	SetIsReplicatedByDefault(true);
}
bool UEnemyAlarmComponent::IsAlarmTag(FName Tag) { return Tag.ToString().StartsWith(AlarmPrefix); }
void UEnemyAlarmComponent::SetCombatActive(bool bActive)
{
	if (!GetOwner()->HasAuthority()) return;
	if (bActive && !bCombatActive) { ++CombatSession; ClearInvestigation(); }
	bCombatActive = bActive;
}
void UEnemyAlarmComponent::ResetForReuse()
{
	if (!GetOwner()->HasAuthority()) return;
	// Session IDs stay monotonic across pool lives; invalidate in-flight old hearing events.
	++CombatSession; BroadcastSession = CombatSession; bCombatActive = false;
	EmittedTag = NAME_None; EmittedPlayer.Reset(); EmittedShip.Reset();
	LastBroadcastTime = LastHeardTime = -DBL_MAX;
	LastHeardPlayer.Reset(); ReceivedTags.Reset(); ClearInvestigation();
}
bool UEnemyAlarmComponent::Broadcast(AActor* ObservedPlayer, float Range, float Loudness, float Cooldown, USoundBase* Sound)
{
	ABaseEnemy* Enemy = Cast<ABaseEnemy>(GetOwner());
	if (!Enemy || !Enemy->HasAuthority() || !Enemy->CanEngageActor(ObservedPlayer) || !bCombatActive) return false;
	if (!IsAlarmPending()) return true;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastBroadcastTime < FMath::Max(0.0f, Cooldown)
		|| (LastHeardPlayer == ObservedPlayer && Now - LastHeardTime < FMath::Max(0.0f, Cooldown)))
	{
		BroadcastSession = CombatSession; return true;
	}
	EmittedFloor = FDeckWalkLocation();
	EmittedShip.Reset();
	if (ADeckEnemy* Deck = Cast<ADeckEnemy>(Enemy))
	{
		if (!Deck->CanMoveOnDeck()) return false;
		EmittedShip = Deck->GetDeckHostShip();
		const UDeckWalkAreaComponent* Area = EmittedShip->GetDeckWalkAreaComponent();
		FDeckTargetAnchor Anchor;
		if (!Area || !UDeckCombatTargetResolverComponent::ResolveFor(Deck, ObservedPlayer, Anchor)) return false;
		EmittedFloor = FDeckWalkLocation(); EmittedFloor.LocalFloor = Anchor.LocalCenter; EmittedFloor.SurfaceId = Anchor.SurfaceId;
	}
	EmittedPlayer = ObservedPlayer;
	BroadcastSession = CombatSession;
	EmittedWorld = ObservedPlayer->GetActorLocation();
	EmittedTime = LastBroadcastTime = Now;
	EmittedTag = FName(*(AlarmPrefix + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	UAISense_Hearing::ReportNoiseEvent(Enemy, EmittedWorld, FMath::Max(0.0f, Loudness), Enemy,
		FMath::Max(0.0f, Range), EmittedTag);
	if (Sound) MulticastAlarmSound(Sound, EmittedWorld);
	return true;
}
bool UEnemyAlarmComponent::Receive(const UEnemyAlarmComponent& Sender, const FAIStimulus& Stimulus, FVector& OutInvestigation)
{
	ABaseEnemy* Enemy = Cast<ABaseEnemy>(GetOwner());
	const ABaseEnemy* Source = Cast<ABaseEnemy>(Sender.GetOwner());
	const double Now = GetWorld()->GetTimeSeconds();
	if (!Enemy || !Enemy->HasAuthority() || !Source || Source == Enemy || !Stimulus.WasSuccessfullySensed()
		|| Stimulus.Tag != Sender.EmittedTag || !Sender.EmittedPlayer.IsValid()
		|| Now - Sender.EmittedTime > 3.0 || !Enemy->CanEngageActor(Sender.EmittedPlayer.Get())
		|| (Source->GetHealthComponent() && Source->GetHealthComponent()->IsDead())
		|| (Enemy->GetHealthComponent() && Enemy->GetHealthComponent()->IsDead())) return false;
	for (auto It = ReceivedTags.CreateIterator(); It; ++It) if (Now - It.Value() > 3.0) It.RemoveCurrent();
	if (ReceivedTags.Contains(Stimulus.Tag)) return false;
	ADeckEnemy* Deck = Cast<ADeckEnemy>(Enemy);
	const ADeckEnemy* SourceDeck = Cast<ADeckEnemy>(Source);
	if (Deck || SourceDeck)
	{
		if (!Deck || !SourceDeck || !Deck->CanMoveOnDeck() || !SourceDeck->IsPoolActive()
			|| Deck->GetDeckHostShip() != Sender.EmittedShip.Get()) return false;
		InvestigationShip = Sender.EmittedShip;
		InvestigationFloor = Sender.EmittedFloor;
		if (!GetInvestigationWorld(OutInvestigation)) { ClearInvestigation(); return false; }
	}
	else OutInvestigation = Sender.EmittedWorld;
	ReceivedTags.Add(Stimulus.Tag, Now);
	LastHeardPlayer = Sender.EmittedPlayer; LastHeardTime = Now;
	return true;
}
bool UEnemyAlarmComponent::GetInvestigationWorld(FVector& Out) const
{
	const ADeckEnemy* Enemy = Cast<ADeckEnemy>(GetOwner());
	const AEnemyShip* Ship = InvestigationShip.Get();
	const UDeckWalkAreaComponent* Area = Ship ? Ship->GetDeckWalkAreaComponent() : nullptr;
	if (!Enemy || !Enemy->CanMoveOnDeck() || Enemy->GetDeckHostShip() != Ship || !Area || !Area->IsReady()
		|| InvestigationFloor.SurfaceId.IsNone()) return false;
	Out = Area->ToWorld(InvestigationFloor.LocalFloor);
	return true;
}
void UEnemyAlarmComponent::ClearInvestigation() { InvestigationShip.Reset(); InvestigationFloor = FDeckWalkLocation(); }
void UEnemyAlarmComponent::CaptureInvestigationWorld(const FVector& Point)
{
	if (!GetOwner()->HasAuthority()) return;
	FVector Existing;
	if (GetInvestigationWorld(Existing) && Existing.Equals(Point, 1.0f)) return;
	ClearInvestigation();
	const ADeckEnemy* Enemy = Cast<ADeckEnemy>(GetOwner());
	AEnemyShip* Ship = Enemy ? Enemy->GetDeckHostShip() : nullptr;
	const UDeckWalkAreaComponent* Area = Ship ? Ship->GetDeckWalkAreaComponent() : nullptr;
	FDeckWalkLocation Floor;
	if (Area && Area->ResolveLocalFloor(Area->ToLocal(Point), NAME_None, Floor))
	{
		InvestigationShip = Ship; InvestigationFloor = Floor;
		InvestigationFloor.LocalFloor = Area->ToLocal(Point);
	}
}
void UEnemyAlarmComponent::MulticastAlarmSound_Implementation(USoundBase* Sound, FVector_NetQuantize Location)
{
	if (GetNetMode() != NM_DedicatedServer && Sound) UGameplayStatics::PlaySoundAtLocation(this, Sound, Location);
}
