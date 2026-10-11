#include "BossAI/BossDeckRecoveryComponent.h"

#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "BaseGameplayTags.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BossAI/BossVanishFeedback.h"
#include "BossAI/ShipBossEnemy.h"
#include "BrainComponent.h"
#include "Components/BaseHealthComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "Engine/World.h"
#include "GAS/Ability/Boss/BossGameplayAbility.h"
#include "GAS/Ability/Boss/GA_BossBasicAttack.h"
#include "ShipAI/EnemyShip.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogBossDeckRecovery, Log, All);

UBossDeckRecoveryComponent::UBossDeckRecoveryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

AShipBossEnemy* UBossDeckRecoveryComponent::GetLiveBoss() const
{
	AShipBossEnemy* Boss = Cast<AShipBossEnemy>(GetOwner());
	return IsValid(Boss) && Boss->HasAuthority() && !Boss->IsActorBeingDestroyed()
		&& Boss->GetHealthComponent() && !Boss->GetHealthComponent()->IsDead()
		&& ObservedShip.IsValid() && Boss->GetHostShip() == ObservedShip.Get() ? Boss : nullptr;
}

UDeckWalkAreaComponent* UBossDeckRecoveryComponent::GetArea() const
{
	return GetLiveBoss() ? ObservedShip->GetDeckWalkAreaComponent() : nullptr;
}

void UBossDeckRecoveryComponent::NotifyHidden()
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(SupportTimer);
	bAwaitingReveal = false;
	UnsupportedChecks = 0;
}

void UBossDeckRecoveryComponent::NotifyRelocated(const FDeckWalkLocation& IntendedFloor)
{
	AShipBossEnemy* Boss = Cast<AShipBossEnemy>(GetOwner());
	if (!Boss || !Boss->HasAuthority() || !IsValid(Boss->GetHostShip())
		|| Boss->GetHealthComponent()->IsDead()) return;
	if (ObservedShip.Get() != Boss->GetHostShip()) ResetRecovery(true);
	ObservedShip = Boss->GetHostShip();
	if (!bRecovering) IntendedLocation = IntendedFloor;
	bAwaitingReveal = true;
	UDeckWalkAreaComponent* Area = GetArea();
	FDeckWalkLocation Support;
	// This sample is diagnostic/safe-history only. Hidden time never counts as
	// consecutive unsupported time, and no query is made just before revealing.
	if (Area && Area->IsReady() && Area->ResolveActorDeckSupport(*Boss, IntendedFloor.SurfaceId, MaximumFeetGap, Support))
		LastSupportedLocation = Support;
	else
		UE_LOG(LogBossDeckRecovery, Verbose, TEXT("Teleport has no nearby deck support. Boss=%s Surface=%s"),
			*GetNameSafe(Boss), *IntendedFloor.SurfaceId.ToString());
}

void UBossDeckRecoveryComponent::NotifyRevealed()
{
	AShipBossEnemy* Boss = GetLiveBoss();
	if (!Boss || !bAwaitingReveal || Boss->IsBossHidden() || !GetWorld()) return;
	UnsupportedChecks = 0;
	MonitoringDeadline = bRecovering ? RecoveryDeadline : GetWorld()->GetTimeSeconds()
		+ FMath::Max(1.f, SupportCheckInterval * (ConsecutiveUnsupportedChecks + 1));
	CheckSupport();
}

void UBossDeckRecoveryComponent::ScheduleSupportCheck()
{
	GetWorld()->GetTimerManager().SetTimer(SupportTimer, this, &ThisClass::CheckSupport,
		FMath::Max(0.02f, SupportCheckInterval), false);
}

void UBossDeckRecoveryComponent::CheckSupport()
{
	AShipBossEnemy* Boss = GetLiveBoss();
	if (!Boss) { ResetRecovery(); return; }
	if (!bAwaitingReveal || Boss->IsBossHidden() || Boss->IsHiddenRelocationActive()) return;
	UDeckWalkAreaComponent* Area = GetArea();
	if (!Area || !Area->IsReady())
	{
		// A graph rebuild is not evidence of unsupported placement.
		UnsupportedChecks = 0;
		if (GetWorld()->GetTimeSeconds() < MonitoringDeadline) ScheduleSupportCheck();
		else if (bRecovering) FinishRecovery(false);
		else
		{
			bAwaitingReveal = false;
			UE_LOG(LogBossDeckRecovery, Warning, TEXT("Post-reveal support check expired while deck was not ready. Boss=%s"), *GetNameSafe(Boss));
		}
		return;
	}
	FDeckWalkLocation Support;
	const FName Surface = bRecovering ? RecoveryLocation.SurfaceId : IntendedLocation.SurfaceId;
	if (Area->ResolveActorDeckSupport(*Boss, Surface, MaximumFeetGap, Support))
	{
		LastSupportedLocation = Support;
		UnsupportedChecks = 0;
		bAwaitingReveal = false;
		// A normal landing ends monitoring immediately: no permanent polling.
		if (bRecovering) FinishRecovery(true);
		return;
	}
	if (++UnsupportedChecks < FMath::Max(2, ConsecutiveUnsupportedChecks))
	{
		ScheduleSupportCheck();
		return;
	}
	bAwaitingReveal = false;
	if (bRecovering) HideForRecovery();
	else BeginRecovery();
}

void UBossDeckRecoveryComponent::CancelBossCombatAbilities(AShipBossEnemy& Boss)
{
	UAbilitySystemComponent* ASC = Boss.GetAbilitySystemComponent();
	if (!ASC) return;
	TArray<FGameplayAbilitySpecHandle> Handles;
	for (const auto& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.IsActive() && Spec.Ability
			&& (Spec.Ability->IsA<UBossGameplayAbility>() || Spec.Ability->IsA<UGA_BossBasicAttack>())) Handles.Add(Spec.Handle);
	}
	// Cancel callbacks may mutate the ability list. Preserve unrelated status/death abilities and effects.
	for (const auto& Handle : Handles) ASC->CancelAbilityHandle(Handle);
}

void UBossDeckRecoveryComponent::BeginRecovery()
{
	AShipBossEnemy* Boss = GetLiveBoss();
	if (!Boss || bRecovering) return;
	bRecovering = true;
	RecoveryAttempts = 0;
	FailedNodes.Reset();
	RecoveryRevision = INDEX_NONE;
	RecoveryDeadline = GetWorld()->GetTimeSeconds() + FMath::Max(0.5f, MaximumRecoverySeconds);
	GetWorld()->GetTimerManager().SetTimer(RecoveryDeadlineTimer,
		FTimerDelegate::CreateUObject(this, &ThisClass::FinishRecovery, false), FMath::Max(0.5f, MaximumRecoverySeconds), false);
	if (auto* AI = Cast<AAIController>(Boss->GetController()))
	{
		if (UBrainComponent* Brain = AI->GetBrainComponent(); Brain && Brain->IsRunning() && !Brain->IsPaused())
		{
			PausedBrain = Brain;
			Brain->PauseLogic(TEXT("Boss deck recovery"));
		}
	}
	if (UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent())
	{
		ASC->AddLooseGameplayTag(State_Boss_Busy);
		ASC->AddLooseGameplayTag(State_Boss_Hidden);
		bOwnsBusyTag = true;
	}
	// Recovery owns hidden state before cancel callbacks run, so their cleanup
	// cannot reveal the boss or overwrite a new recovery destination.
	CancelBossCombatAbilities(*Boss);
	if (!GetLiveBoss()) { ResetRecovery(); return; }
	Boss->ClearDestination();
	if (auto* AI = Cast<AAIController>(Boss->GetController()))
	{
		if (UBlackboardComponent* Blackboard = AI->GetBlackboardComponent();
			Blackboard && Blackboard->GetKeyID(TEXT("DestinationLocation")) != FBlackboard::InvalidKey)
			Blackboard->ClearValue(TEXT("DestinationLocation"));
	}
	UE_LOG(LogBossDeckRecovery, Log, TEXT("Begin deck recovery after %d unsupported checks. Boss=%s"), UnsupportedChecks, *GetNameSafe(Boss));
	HideForRecovery();
}

bool UBossDeckRecoveryComponent::HasRecoveryTimedOut() const
{
	return !GetWorld() || GetWorld()->GetTimeSeconds() >= RecoveryDeadline;
}

void UBossDeckRecoveryComponent::HideForRecovery()
{
	AShipBossEnemy* Boss = GetLiveBoss();
	if (!Boss) { ResetRecovery(); return; }
	if (HasRecoveryTimedOut() || RecoveryAttempts >= FMath::Max(1, MaximumRecoveryAttempts))
	{
		FinishRecovery(false);
		return;
	}
	if (UDeckWalkAreaComponent* Area = GetArea())
	{
		if (RecoveryLocation.Revision == Area->GetRevision()) FailedNodes.Add(RecoveryLocation.NodeIndex);
		Area->ReleaseLocationClaim(Boss);
	}
	const FVector Departure = Boss->GetActorLocation();
	if (!Boss->IsHiddenRelocationActive())
	{
		if (!Boss->BeginHiddenRelocation()) { FinishRecovery(false); return; }
		BossVanishFeedback::ExecuteAtLocation(Boss, GameplayCue_Boss_Vanish_Departure, Departure);
	}
	GetWorld()->GetTimerManager().SetTimer(RecoveryTimer, this, &ThisClass::RelocateForRecovery,
		FMath::Max(0.01f, HiddenLeadTime), false);
}

void UBossDeckRecoveryComponent::RelocateForRecovery()
{
	AShipBossEnemy* Boss = GetLiveBoss();
	if (!Boss) { ResetRecovery(); return; }
	if (HasRecoveryTimedOut()) { FinishRecovery(false); return; }
	UDeckWalkAreaComponent* Area = GetArea();
	if (!Area || !Area->IsReady())
	{
		GetWorld()->GetTimerManager().SetTimer(RecoveryTimer, this, &ThisClass::RelocateForRecovery,
			FMath::Max(0.02f, SupportCheckInterval), false);
		return;
	}
	if (RecoveryRevision != Area->GetRevision())
	{
		FailedNodes.Reset();
		RecoveryRevision = Area->GetRevision();
	}
	++RecoveryAttempts;
	FDeckWalkLocation Candidate;
	bool bFound = Area->FindRecoveryLocation(IntendedLocation.LocalFloor, IntendedLocation.SurfaceId, *Boss, FailedNodes, Candidate);
	if (!bFound && LastSupportedLocation.SurfaceId == IntendedLocation.SurfaceId)
		bFound = Area->FindRecoveryLocation(LastSupportedLocation.LocalFloor, LastSupportedLocation.SurfaceId, *Boss, FailedNodes, Candidate);
	if (!bFound)
	{
		FDeckWalkLocation Spawn;
		const UDeckWaypointComponent* Point = ObservedShip->GetDeckWaypoint(Boss->GetInitialSpawnPointId());
		if (Point && Area->ResolveWaypoint(*Point, Spawn) && Spawn.SurfaceId == IntendedLocation.SurfaceId)
			bFound = Area->FindRecoveryLocation(Spawn.LocalFloor, Spawn.SurfaceId, *Boss, FailedNodes, Candidate);
	}
	FTransform Transform;
	if (!bFound || !Area->TryClaimLocation(Candidate, *Boss))
	{
		if (Candidate.NodeIndex != INDEX_NONE) FailedNodes.Add(Candidate.NodeIndex);
		HideForRecovery();
		return;
	}
	RecoveryLocation = Candidate;
	if (!Area->ResolveLocationTransform(Candidate, *Boss, Transform)
		|| !Boss->RelocateWhileHidden(Transform, Area->GetFloorComponent(Candidate)))
	{
		FailedNodes.Add(Candidate.NodeIndex);
		HideForRecovery();
		return;
	}
	NotifyRelocated(Candidate);
	GetWorld()->GetTimerManager().SetTimer(RecoveryTimer, this, &ThisClass::RevealAfterRecovery,
		FMath::Max(0.01f, HiddenSettleTime), false);
}

void UBossDeckRecoveryComponent::RevealAfterRecovery()
{
	AShipBossEnemy* Boss = GetLiveBoss();
	if (!Boss) { ResetRecovery(); return; }
	if (HasRecoveryTimedOut()) { FinishRecovery(false); return; }
	// Restore visibility first. FinishHiddenRelocation then notifies us to probe
	// the actual feet against the current ship transform, never before reveal.
	Boss->FinishHiddenRelocation(true);
	if (GetLiveBoss() && !Boss->IsBossHidden())
		BossVanishFeedback::ExecuteAtLocation(Boss, GameplayCue_Boss_Vanish_Arrival, Boss->GetActorLocation());
}

void UBossDeckRecoveryComponent::ReleaseRecoveryResources(bool bResumeLogic)
{
	AShipBossEnemy* Boss = Cast<AShipBossEnemy>(GetOwner());
	if (RecoveryLocation.NodeIndex != INDEX_NONE && ObservedShip.IsValid())
		if (UDeckWalkAreaComponent* Area = ObservedShip->GetDeckWalkAreaComponent()) Area->ReleaseLocationClaim(Boss);
	if (bOwnsBusyTag && Boss)
	{
		if (UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent())
		{
			ASC->RemoveLooseGameplayTag(State_Boss_Busy);
			ASC->RemoveLooseGameplayTag(State_Boss_Hidden);
		}
	}
	bOwnsBusyTag = false;
	if (UBrainComponent* Brain = PausedBrain.Get(); Brain && bResumeLogic && GetLiveBoss())
	{
		Brain->ResumeLogic(TEXT("Boss deck recovery complete"));
		Brain->RestartLogic();
	}
	PausedBrain.Reset();
	RecoveryLocation = FDeckWalkLocation();
}

void UBossDeckRecoveryComponent::FinishRecovery(bool bSupported)
{
	if (!bRecovering) return;
	AShipBossEnemy* Boss = GetLiveBoss();
	const TWeakObjectPtr<UBrainComponent> Brain = PausedBrain;
	bRecovering = false;
	bAwaitingReveal = false;
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(SupportTimer);
		GetWorld()->GetTimerManager().ClearTimer(RecoveryTimer);
		GetWorld()->GetTimerManager().ClearTimer(RecoveryDeadlineTimer);
	}
	ReleaseRecoveryResources(bSupported);
	if (!Boss) return;
	if (!bSupported)
	{
		// No unchecked relocation, infinite hidden state, death or reward is manufactured.
		if (Brain.IsValid()) Brain->StopLogic(TEXT("Boss deck recovery exhausted"));
		Boss->FinishHiddenRelocation(true);
		UE_LOG(LogBossDeckRecovery, Error, TEXT("Deck recovery exhausted; combat stopped. Boss=%s Attempts=%d Surface=%s"),
			*GetNameSafe(Boss), RecoveryAttempts, *IntendedLocation.SurfaceId.ToString());
	}
	else
		UE_LOG(LogBossDeckRecovery, Log, TEXT("Deck recovery completed. Boss=%s Attempts=%d"), *GetNameSafe(Boss), RecoveryAttempts);
}

void UBossDeckRecoveryComponent::ResetRecovery(bool bResumeLogic)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(SupportTimer);
		GetWorld()->GetTimerManager().ClearTimer(RecoveryTimer);
		GetWorld()->GetTimerManager().ClearTimer(RecoveryDeadlineTimer);
	}
	bRecovering = false;
	bAwaitingReveal = false;
	ReleaseRecoveryResources(bResumeLogic);
	ObservedShip.Reset();
	IntendedLocation = LastSupportedLocation = FDeckWalkLocation();
	FailedNodes.Reset();
	UnsupportedChecks = RecoveryAttempts = 0;
}

void UBossDeckRecoveryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetRecovery();
	Super::EndPlay(EndPlayReason);
}
