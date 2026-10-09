#include "GAS/Tasks/AbilityTask_BossVanishRelocation.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemComponent.h"
#include "AI/PointSelectionFailure.h"
#include "BaseGameplayTags.h"
#include "BossAI/ShipBossEnemy.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "GAS/Ability/Boss/BossGameplayAbility.h"
#include "ShipAI/EnemyShip.h"
#include "TimerManager.h"

UAbilityTask_BossVanishRelocation* UAbilityTask_BossVanishRelocation::Relocate(
	UGameplayAbility* Owner, AShipBossEnemy* InBoss, AActor* Target, UAnimMontage* Montage,
	float Delay, float HiddenTime, float Settle)
{
	auto* Task = NewAbilityTask<UAbilityTask_BossVanishRelocation>(Owner);
	Task->Boss = InBoss;
	Task->LockedTarget = Target;
	Task->CapturedShip = InBoss ? InBoss->GetHostShip() : nullptr;
	Task->PreparationMontage = Montage;
	Task->PreparationDelay = FMath::Max(0.f, Delay);
	Task->HiddenDuration = FMath::Max(0.01f, HiddenTime);
	Task->SettleTime = FMath::Max(0.01f, Settle);
	Task->CapturedDestination = InBoss ? InBoss->GetDestinationLocation() : FDeckWalkLocation();
	return Task;
}

UAbilityTask_BossVanishRelocation* UAbilityTask_BossVanishRelocation::RelocateToSnapshot(
	UGameplayAbility* Owner, AShipBossEnemy* InBoss, FVector TargetLocalFloor, UAnimMontage* Montage,
	float Delay, float HiddenTime, float Settle)
{
	auto* Task = Relocate(Owner, InBoss, nullptr, Montage, Delay, HiddenTime, Settle);
	Task->bUseSnapshotTarget = true;
	Task->SnapshotLocalFloor = TargetLocalFloor;
	return Task;
}

bool UAbilityTask_BossVanishRelocation::IsTargetUsable() const
{
	if (!Boss || !CapturedShip.IsValid() || Boss->GetHostShip() != CapturedShip.Get()) return false;
	if (!bUseSnapshotTarget) return Boss->CanEngageActor(LockedTarget.Get());
	const AEnemyShip* Ship = Boss->GetHostShip();
	return IsValid(Ship) && Ship->GetDeckWalkAreaComponent() && !SnapshotLocalFloor.ContainsNaN();
}

FVector UAbilityTask_BossVanishRelocation::ResolveFacingPosition() const
{
	if (bUseSnapshotTarget)
		return Boss->GetHostShip()->GetDeckWalkAreaComponent()->ToWorld(SnapshotLocalFloor);
	return LockedTarget->GetActorLocation();
}

void UAbilityTask_BossVanishRelocation::Activate()
{
	FTransform Destination;
	if (!Boss || !Boss->HasAuthority() || !GetWorld() || !IsTargetUsable())
	{
		Fail();
		return;
	}
	if (!Boss->HasDestination() || !Boss->ResolveDestinationTransform(Destination))
	{
		EnemyPointSelectionFailure::Log(this, Boss, TEXT("Cannot resolve the preselected Vanish point."));
		Fail();
		return;
	}
	if (PreparationMontage)
	{
		PreparationMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			Ability, TEXT("BossVanishPreparation"), PreparationMontage);
		if (!PreparationMontageTask) { Fail(); return; }
		PreparationMontageTask->OnInterrupted.AddDynamic(this, &ThisClass::Fail);
		PreparationMontageTask->OnCancelled.AddDynamic(this, &ThisClass::Fail);
		PreparationMontageTask->ReadyForActivation();
		if (!IsActive()) return;
	}
	if (PreparationDelay <= 0.f) BeginHidden();
	else GetWorld()->GetTimerManager().SetTimer(PhaseTimer, this, &ThisClass::BeginHidden, PreparationDelay, false);
}

void UAbilityTask_BossVanishRelocation::BeginHidden()
{
	const FVector DepartureLocation = Boss ? Boss->GetActorLocation() : FVector::ZeroVector;
	if (!IsActive() || !Boss || !IsTargetUsable()
		|| !Boss->BeginHiddenRelocation()) { Fail(); return; }
	bStartedHiding = true;
	bOwnsHiddenState = true;
	// Broadcast only after hiding actually succeeded, using the captured departure transform.
	if (ShouldBroadcastAbilityTaskDelegates()) OnDeparture.Broadcast(DepartureLocation);
	if (!IsActive()) return;
	StopPreparationMontage();
	if (UAbilitySystemComponent* ASC = AbilitySystemComponent.Get())
	{
		auto Spec = ASC->MakeOutgoingSpec(UBossAbilityStateEffect::StaticClass(), 1.f, ASC->MakeEffectContext());
		if (Spec.IsValid())
		{
			Spec.Data->SetDuration(HiddenDuration + SettleTime + 0.25f, true);
			Spec.Data->DynamicGrantedTags.AddTag(State_Boss_Hidden);
			HiddenStateHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		}
	}
	GetWorld()->GetTimerManager().SetTimer(PhaseTimer, this, &ThisClass::RelocateHidden, HiddenDuration, false);
}

void UAbilityTask_BossVanishRelocation::RelocateHidden()
{
	FTransform Destination;
	if (!IsActive() || !Boss || !IsTargetUsable()) { Fail(); return; }
	if (Boss->GetDestinationLocation().NodeIndex != CapturedDestination.NodeIndex
		|| Boss->GetDestinationLocation().SurfaceId != CapturedDestination.SurfaceId
		|| Boss->GetDestinationLocation().Revision != CapturedDestination.Revision
		|| !Boss->ResolveDestinationTransform(Destination))
	{
		EnemyPointSelectionFailure::Log(this, Boss, TEXT("Captured Vanish point became invalid before relocation."));
		Fail();
		return;
	}

	const FVector Up = Boss->GetHostShip() && Boss->GetHostShip()->GetShipDeckMesh()
		? Boss->GetHostShip()->GetShipDeckMesh()->GetUpVector().GetSafeNormal() : FVector::UpVector;
	const FVector Forward = FVector::VectorPlaneProject(ResolveFacingPosition() - Destination.GetLocation(), Up).GetSafeNormal();
	if (!Forward.IsNearlyZero()) Destination.SetRotation(FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat());
	if (!Boss->RelocateWhileHidden(Destination))
	{
		EnemyPointSelectionFailure::Log(this, Boss, TEXT("Vanish point is no longer available for placement."));
		Fail();
		return;
	}
	Boss->MarkDestinationReached();
	GetWorld()->GetTimerManager().SetTimer(PhaseTimer, this, &ThisClass::Reveal, SettleTime, false);
}

void UAbilityTask_BossVanishRelocation::Reveal()
{
	if (!IsActive() || !Boss || !IsTargetUsable() || !Boss->IsHiddenRelocationActive()) { Fail(); return; }
	ClearHiddenState();
	if (ShouldBroadcastAbilityTaskDelegates()) OnArrival.Broadcast(Boss->GetActorLocation());
	if (ShouldBroadcastAbilityTaskDelegates()) OnRevealed.Broadcast();
	EndTask();
}

void UAbilityTask_BossVanishRelocation::Fail()
{
	ClearHiddenState();
	if (ShouldBroadcastAbilityTaskDelegates()) OnFailed.Broadcast();
	EndTask();
}

void UAbilityTask_BossVanishRelocation::StopPreparationMontage()
{
	if (!PreparationMontageTask) return;
	PreparationMontageTask->OnInterrupted.RemoveAll(this);
	PreparationMontageTask->OnCancelled.RemoveAll(this);
	if (UAbilitySystemComponent* ASC = AbilitySystemComponent.Get(); ASC && ASC->GetCurrentMontage() == PreparationMontage)
		ASC->CurrentMontageStop(0.f);
	PreparationMontageTask->EndTask();
	PreparationMontageTask = nullptr;
}

void UAbilityTask_BossVanishRelocation::ClearHiddenState()
{
	if (bOwnsHiddenState && Boss)
	{
		Boss->FinishHiddenRelocation();
		bOwnsHiddenState = false;
	}
	if (HiddenStateHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = AbilitySystemComponent.Get()) ASC->RemoveActiveGameplayEffect(HiddenStateHandle);
		HiddenStateHandle.Invalidate();
	}
}

void UAbilityTask_BossVanishRelocation::OnDestroy(bool bAbilityEnded)
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(PhaseTimer);
	StopPreparationMontage();
	ClearHiddenState();
	Super::OnDestroy(bAbilityEnded);
}
