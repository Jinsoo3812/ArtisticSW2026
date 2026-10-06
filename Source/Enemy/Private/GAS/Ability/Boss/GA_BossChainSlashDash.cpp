#include "GAS/Ability/Boss/GA_BossChainSlashDash.h"

#include "AbilitySystemComponent.h"
#include "AI/PointSelectionFailure.h"
#include "BaseGameplayTags.h"
#include "BossAI/BossVanishFeedback.h"
#include "BossAI/ShipBossEnemy.h"
#include "Components/CombatHitResolverComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "GAS/Tasks/AbilityTask_BossSlashDashExecution.h"
#include "GAS/Tasks/AbilityTask_BossTargetSnapshot.h"
#include "GAS/Tasks/AbilityTask_BossVanishRelocation.h"
#include "ShipAI/EnemyShip.h"
#include "TimerManager.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogBossChainSlashDash, Log, All);

UGA_BossChainSlashDash::UGA_BossChainSlashDash()
{
	SetBossAbilityTags(GameplayAbility_Boss_ChainSlashDash, Cooldown_Boss_ChainSlashDash);
	CooldownDuration = 10.f;
	Steps.AddDefaulted(2);
	Steps[1].WaitBeforeDash = 0.f;
	Steps[1].DistanceBeyondPlayer = 500.f;
	DepartureGameplayCueTag = GameplayCue_Boss_Vanish_Departure;
	ArrivalGameplayCueTag = GameplayCue_Boss_Vanish_Arrival;
}

void UGA_BossChainSlashDash::ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	// Commit checks cost/cooldown once; this pattern's cooldown starts after its last step or cancellation.
}

bool UGA_BossChainSlashDash::ValidateSequenceConfig(FString& OutError) const
{
	if (Steps.IsEmpty()) { OutError = TEXT("Steps must contain at least one vanish + dash cycle."); return false; }
	if (!FMath::IsFinite(DashDuration) || DashDuration < 0.05f || !FMath::IsFinite(DashHitRadius) || DashHitRadius <= 0.f
		|| !FMath::IsFinite(MinimumDashDistance) || MinimumDashDistance < 1.f
		|| !FMath::IsFinite(SelectionSettings.MaximumDashDistance) || SelectionSettings.MaximumDashDistance < MinimumDashDistance
		|| !FMath::IsFinite(PreparationDelay) || PreparationDelay < 0.f
		|| !FMath::IsFinite(HiddenDuration) || HiddenDuration <= 0.f
		|| !FMath::IsFinite(RelocationSettleTime) || RelocationSettleTime <= 0.f)
	{
		OutError = TEXT("Dash distance, duration, radius or vanish timings are invalid.");
		return false;
	}
	for (int32 Index = 0; Index < Steps.Num(); ++Index)
	{
		const auto& Step = Steps[Index];
		if (!UAbilityTask_BossSlashDashExecution::ValidateConfig(MontageConfig, Step.WaitBeforeDash, OutError, Index > 0)
			|| Step.WaitBeforeDash < 0.f || !FMath::IsFinite(Step.DistanceBeyondPlayer) || Step.DistanceBeyondPlayer <= 0.f)
		{
			OutError = FString::Printf(TEXT("Steps[%d]: %s Check WaitBeforeDash and DistanceBeyondPlayer."), Index, *OutError);
			return false;
		}
	}
	return true;
}

#if WITH_EDITOR
EDataValidationResult UGA_BossChainSlashDash::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult ParentResult = Super::IsDataValid(Context);
	FString Error;
	if (!ValidateSequenceConfig(Error)) { Context.AddError(FText::FromString(Error)); return EDataValidationResult::Invalid; }
	return ParentResult == EDataValidationResult::Invalid ? ParentResult : EDataValidationResult::Valid;
}
#endif

void UGA_BossChainSlashDash::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// Skip the single-dash activation while retaining its settings and execution adapter.
	UBossGameplayAbility::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	StepIndex = 0;
	bConsumedSequence = false;
	bEndingSequence = false;
	AShipBossEnemy* Boss = GetBossAvatar();
	AActor* Target = GetBossTarget();
	SequenceShip = Boss ? Boss->GetHostShip() : nullptr;
	FString Error;
	if (!Boss || !Boss->HasAuthority() || !Boss->CanEngageActor(Target)) { FinishSequence(true); return; }
	if (!ValidateSequenceConfig(Error))
	{
		UE_LOG(LogBossChainSlashDash, Warning, TEXT("Invalid chain configuration: %s"), *Error);
		FinishSequence(true);
		return;
	}
	TargetSnapshotTask = UAbilityTask_BossTargetSnapshot::Track(this, Boss, Target);
	TargetSnapshotTask->ReadyForActivation();
	if (!TargetSnapshotTask->GetSnapshot().bValid) { CancelForPointFailure(TEXT("No valid target deck point.")); return; }
	FBossDestinationSelectionSettings Settings = SelectionSettings;
	Settings.MinimumDashTravelDistance = FMath::Max(Settings.MinimumDashTravelDistance, MinimumDashDistance);
	FDeckWalkLocation Destination;
	if (!UBossDeckPointSelector::SelectChainRelocation(*Boss, TargetSnapshotTask->GetSnapshot(),
		Steps[0].DistanceBeyondPlayer, Settings, Target, Destination)
		|| !Boss->TrySetDestinationLocation(Destination, false))
	{
		CancelForPointFailure(TEXT("No valid initial chain relocation point."));
		return;
	}
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo)) { FinishSequence(true); return; }
	StartNextStep();
}

void UGA_BossChainSlashDash::StartNextStep()
{
	if (!IsActive() || bEndingSequence || !Steps.IsValidIndex(StepIndex)) return;
	AShipBossEnemy* Boss = GetBossAvatar();
	if (!Boss || !SequenceShip.IsValid() || Boss->GetHostShip() != SequenceShip.Get() || !TargetSnapshotTask) { FinishSequence(true); return; }
	const auto Snapshot = TargetSnapshotTask->GetSnapshot();
	if (StepIndex > 0)
	{
		FBossDestinationSelectionSettings Settings = SelectionSettings;
		Settings.MinimumDashTravelDistance = FMath::Max(Settings.MinimumDashTravelDistance, MinimumDashDistance);
		FDeckWalkLocation Destination;
		if (!UBossDeckPointSelector::SelectChainRelocation(*Boss, Snapshot, Steps[StepIndex].DistanceBeyondPlayer,
			Settings, TargetSnapshotTask->GetTrackedActor(), Destination)
			|| !Boss->TrySetDestinationLocation(Destination, false))
		{
			CancelForPointFailure(TEXT("No valid continuation relocation point."));
			return;
		}
	}
	// Existing BP wait/vanish overrides must not reintroduce a pause between slashes.
	const bool bContinuation = StepIndex > 0;
	RelocationTask = UAbilityTask_BossVanishRelocation::RelocateToSnapshot(this, Boss, Snapshot.LocalFloor,
		bContinuation ? nullptr : PreparationMontage, bContinuation ? 0.f : PreparationDelay,
		bContinuation ? 0.01f : HiddenDuration, bContinuation ? 0.01f : RelocationSettleTime);
	RelocationTask->OnRevealed.AddDynamic(this, &ThisClass::OnRevealed);
	RelocationTask->OnFailed.AddDynamic(this, &ThisClass::OnRelocationFailed);
	RelocationTask->OnDeparture.AddDynamic(this, &ThisClass::OnDeparture);
	RelocationTask->OnArrival.AddDynamic(this, &ThisClass::OnArrival);
	RelocationTask->ReadyForActivation();
}

void UGA_BossChainSlashDash::OnRevealed()
{
	RelocationTask = nullptr;
	if (!IsActive() || bEndingSequence || !Steps.IsValidIndex(StepIndex)) return;
	AShipBossEnemy* Boss = GetBossAvatar();
	AEnemyShip* Ship = Boss ? Boss->GetHostShip() : nullptr;
	const UDeckWalkAreaComponent* Area = IsValid(Ship) ? Ship->GetDeckWalkAreaComponent() : nullptr;
	FDeckWalkLocation Start, Goal;
	FBossDestinationSelectionSettings Settings = SelectionSettings;
	Settings.MinimumDashTravelDistance = FMath::Max(Settings.MinimumDashTravelDistance, MinimumDashDistance);
	if (!Area || !SequenceShip.IsValid() || Ship != SequenceShip.Get() || !TargetSnapshotTask
		|| !Area->ResolveActorOnDeck(*Boss, Start))
	{
		CancelForPointFailure(TEXT("Cannot resolve Samurai's revealed deck point."));
		return;
	}
	// Use precise feet XY for the ray; ResolveActorOnDeck normally returns a sampled node center.
	FVector LocalFeet = Area->ToLocal(Area->GetActorFeetWorld(*Boss));
	LocalFeet.Z = Start.LocalFloor.Z;
	FDeckWalkLocation PreciseStart;
	if (!Area->ResolvePreciseLocalFloor(LocalFeet, Start.SurfaceId, PreciseStart)
		|| !UBossDeckPointSelector::SelectDashBeyondSnapshot(*Boss, PreciseStart, TargetSnapshotTask->GetSnapshot(),
			Steps[StepIndex].DistanceBeyondPlayer, Settings, TargetSnapshotTask->GetTrackedActor(), Goal)
		|| !Boss->TrySetDestinationLocation(Goal, false))
	{
		CancelForPointFailure(TEXT("No valid dash point beyond the target."));
		return;
	}
	const bool bContinuation = StepIndex > 0;
	const float WaitBeforeDash = bContinuation ? 0.f : Steps[StepIndex].WaitBeforeDash;
	UE_LOG(LogBossChainSlashDash, Log, TEXT("Step %d/%d: wait %.2fs, beyond %.1fcm, frozen target=%d"),
		StepIndex + 1, Steps.Num(), WaitBeforeDash, Steps[StepIndex].DistanceBeyondPlayer, TargetSnapshotTask->IsFrozen());
	if (!StartDashExecution(WaitBeforeDash, bContinuation, StepIndex + 1 < Steps.Num())) FinishSequence(true);
}

void UGA_BossChainSlashDash::HandleExecutionCompleted()
{
	ExecutionTask = nullptr;
	if (!IsActive() || bEndingSequence) return;
	if (auto* Boss = GetBossAvatar())
		if (auto* Resolver = Boss->FindComponentByClass<UCombatHitResolverComponent>()) Resolver->CloseWindow();
	if (++StepIndex >= Steps.Num()) { FinishSequence(false); return; }
	if (GetWorld()) NextStepTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::StartNextStep);
	else FinishSequence(true);
}

void UGA_BossChainSlashDash::HandleExecutionFailed()
{
	ExecutionTask = nullptr;
	FinishSequence(true);
}

void UGA_BossChainSlashDash::OnRelocationFailed()
{
	FinishSequence(true);
}

void UGA_BossChainSlashDash::OnDeparture(FVector Location)
{
	bConsumedSequence = true;
	BossVanishFeedback::ExecuteAtLocation(GetBossAvatar(), DepartureGameplayCueTag, Location);
}

void UGA_BossChainSlashDash::OnArrival(FVector Location)
{
	BossVanishFeedback::ExecuteAtLocation(GetBossAvatar(), ArrivalGameplayCueTag, Location);
}

void UGA_BossChainSlashDash::FinishSequence(bool bCancelled)
{
	if (IsActive() && !bEndingSequence) EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bCancelled);
}

void UGA_BossChainSlashDash::CancelForPointFailure(const TCHAR* Reason)
{
	EnemyPointSelectionFailure::Log(this, GetBossAvatar(), Reason);
	FinishSequence(true);
}

void UGA_BossChainSlashDash::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsActive() || bEndingSequence) return;
	bEndingSequence = true;
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(NextStepTimer);
	if (RelocationTask)
	{
		bConsumedSequence |= RelocationTask->HasStartedHiding();
		RelocationTask->OnRevealed.RemoveAll(this);
		RelocationTask->OnFailed.RemoveAll(this);
		RelocationTask->OnDeparture.RemoveAll(this);
		RelocationTask->OnArrival.RemoveAll(this);
		RelocationTask->EndTask();
		RelocationTask = nullptr;
	}
	if (TargetSnapshotTask) { TargetSnapshotTask->EndTask(); TargetSnapshotTask = nullptr; }
	SequenceShip.Reset();
	if (bConsumedSequence)
	{
		bConsumedSequence = false;
		UBossGameplayAbility::ApplyCooldown(Handle, ActorInfo, ActivationInfo);
	}
	if (auto* Boss = GetBossAvatar()) Boss->ClearDestination();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
