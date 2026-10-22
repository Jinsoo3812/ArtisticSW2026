#include "GAS/Tasks/AbilityTask_BossSlashDashExecution.h"
#include "GAS/Ability/Boss/GA_BossDashSlash.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "AI/PointSelectionFailure.h"
#include "Animation/AnimMontage.h"
#include "BaseGameplayTags.h"
#include "BossAI/ShipBossEnemy.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GASAttributeDamageGameplayEffect.h"
#include "GAS/SWCombatEffectContextLibrary.h"
#include "GameplayCue/PathCombatPresentationDataAsset.h"
#include "ShipAI/EnemyShip.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogBossSlashDashExecution, Log, All);

UAbilityTask_BossSlashDashExecution* UAbilityTask_BossSlashDashExecution::Execute(
	UGameplayAbility* Owner, AShipBossEnemy* InBoss, const FDashSlashMontageConfig& Montage,
	float Duration, float MinimumDistance, float HitRadius, UPathCombatPresentationDataAsset* Presentation,
	FGameplayTag ChargeCue, float Wait, bool bInSkipWindup, bool bInSkipRecovery)
{
	auto* Task = NewAbilityTask<UAbilityTask_BossSlashDashExecution>(Owner);
	Task->BossAvatar = InBoss;
	Task->MontageConfig = Montage;
	Task->DashDuration = Duration;
	Task->MinimumDashDistance = MinimumDistance;
	Task->DashHitRadius = HitRadius;
	Task->PathPresentation = Presentation;
	Task->ChargingGameplayCueTag = ChargeCue;
	Task->TotalWaitOverride = Wait;
	Task->bSkipWindup = bInSkipWindup;
	Task->bSkipRecovery = bInSkipRecovery;
	return Task;
}

UAbilitySystemComponent* UAbilityTask_BossSlashDashExecution::GetAbilitySystemComponentFromActorInfo() const
{
	return AbilitySystemComponent.Get();
}

bool UAbilityTask_BossSlashDashExecution::ValidateConfig(const FDashSlashMontageConfig& Montage, float Wait, FString& OutError,
	bool bSkipWindup)
{
	// Reuse precisely the same authoring contract as the executing task.
	auto* Validator = NewObject<UAbilityTask_BossSlashDashExecution>();
	Validator->MontageConfig = Montage;
	if (!Validator->ValidateMontageConfig(OutError)) return false;
	if (!FMath::IsFinite(Wait) || (!bSkipWindup && Wait >= 0.f && Wait < Validator->GetSectionDurationSeconds(Montage.WindupEnterSectionName)))
	{
		OutError = TEXT("WaitBeforeDash must include the entire Windup entry section.");
		return false;
	}
	return true;
}

void UAbilityTask_BossSlashDashExecution::Activate()
{
	if (!BossAvatar.IsValid() || !BossAvatar->HasAuthority() || !Ability || !GetWorld()) { FinishDash(true); return; }
	Phase = EDashSlashPhase::Inactive;
	bDashStarted = false;
	bSlashFinished = false;
	bDestinationReached = false;
	bFinishing = false;
	CapturedDeckMesh.Reset();
	CapturedDestinationLocation = FDeckWalkLocation();
	CapturedStartLocation = FDeckWalkLocation();
	CommittedPath = FSWPathCuePayload();
	TelegraphEffectHandle.Invalidate();
	bMovementLocked = false;

	FString MontageError;
	FString PathError;
	if (!CapturePreselectedDestination() || !ValidateCommittedPath(PathError))
	{
		EnemyPointSelectionFailure::Log(this, GetBossAvatar(), PathError.IsEmpty()
			? TEXT("Cannot capture the preselected dash point.") : *PathError);
		FinishDash(true);
		return;
	}
	if (!ValidateMontageConfig(MontageError))
	{
		UE_LOG(LogBossSlashDashExecution, Error, TEXT("Cannot activate DashSlash: %s"), *MontageError);
		FinishDash(true);
		return;
	}
	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		Ability,
		TEXT("BossDashSlashMontage"),
		MontageConfig.Montage,
		MontageConfig.PlayRate,
		bSkipWindup ? MontageConfig.AttackSectionName : MontageConfig.WindupEnterSectionName,
		true);
	if (!MontageTask)
	{
		FinishDash(true);
		return;
	}

	MontageTask->OnCompleted.AddDynamic(this, &UAbilityTask_BossSlashDashExecution::HandleMontageCompleted);
	MontageTask->OnBlendOut.AddDynamic(this, &UAbilityTask_BossSlashDashExecution::HandleMontageBlendOut);
	MontageTask->OnInterrupted.AddDynamic(this, &UAbilityTask_BossSlashDashExecution::HandleMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &UAbilityTask_BossSlashDashExecution::HandleMontageInterrupted);
	MontageTask->ReadyForActivation();
	if (!IsActive() || bFinishing)
	{
		return;
	}
	ConfigureMontageSections();

	AShipBossEnemy* Boss = GetBossAvatar();
	if (!Boss)
	{
		FinishDash(true);
		return;
	}
	if (bSkipWindup)
	{
		// Continuations start the slash and movement together, without another charge.
		Phase = EDashSlashPhase::DashAttacking;
		StartExecutedPathPresentation();
		Boss->GetWorldTimerManager().SetTimer(WatchdogTimer, this, &ThisClass::HandleMontageInterrupted,
			DashDuration + GetSectionDurationSeconds(MontageConfig.AttackSectionName) + MontageConfig.RecoveryTimeout + 2.f, false);
		BeginDash();
		return;
	}
	if (!LockMovementToCommittedStart())
	{
		EnemyPointSelectionFailure::Log(this, Boss, TEXT("Cannot resolve the committed dash start point."));
		FinishDash(true);
		return;
	}
	StartPathTelegraph();
	StartChargingCue();
	// A malformed montage or invalidated deck must not leave an infinite cue or Busy state.
	const float Wait = TotalWaitOverride >= 0.f ? TotalWaitOverride : Boss->GetBalancedTelegraphDuration(GetSectionDurationSeconds(MontageConfig.WindupEnterSectionName) + MontageConfig.WindupHoldDuration);
	Boss->GetWorldTimerManager().SetTimer(WatchdogTimer, this, &ThisClass::HandleMontageInterrupted, Wait + DashDuration + GetSectionDurationSeconds(MontageConfig.AttackSectionName) + MontageConfig.RecoveryTimeout + 2.f, false);

	Phase = EDashSlashPhase::WindupEntering;
	const float LeadInDuration = GetSectionDurationSeconds(MontageConfig.WindupEnterSectionName);
	if (LeadInDuration <= KINDA_SMALL_NUMBER)
	{
		BeginWindupHold();
	}
	else
	{
		Boss->GetWorldTimerManager().SetTimer(
			WindupLeadInTimerHandle,
			this,
			&UAbilityTask_BossSlashDashExecution::BeginWindupHold,
			LeadInDuration,
			false);
	}
}

void UAbilityTask_BossSlashDashExecution::BeginWindupHold()
{
	if (Phase != EDashSlashPhase::WindupEntering || bFinishing)
	{
		return;
	}
	Phase = EDashSlashPhase::WindupHolding;

	AShipBossEnemy* Boss = GetBossAvatar();
	if (!Boss)
	{
		FinishDash(true);
		return;
	}
	const float LeadIn = GetSectionDurationSeconds(MontageConfig.WindupEnterSectionName);
	const float HoldDuration = FMath::Max(0.f,
		(TotalWaitOverride >= 0.f ? TotalWaitOverride : Boss->GetBalancedTelegraphDuration(LeadIn + MontageConfig.WindupHoldDuration)) - LeadIn);
	if (HoldDuration <= KINDA_SMALL_NUMBER)
	{
		ReleaseWindupAndBeginDash();
		return;
	}

	Boss->GetWorldTimerManager().SetTimer(
		WindupHoldTimerHandle,
		this,
		&UAbilityTask_BossSlashDashExecution::ReleaseWindupAndBeginDash,
		HoldDuration,
		false);
}

void UAbilityTask_BossSlashDashExecution::ReleaseWindupAndBeginDash()
{
	if (Phase != EDashSlashPhase::WindupHolding || bFinishing)
	{
		return;
	}

	if (!MontageTask || !TransitionMontagePhase(
		EDashSlashPhase::WindupHolding,
		EDashSlashPhase::DashAttacking,
		MontageConfig.AttackSectionName))
	{
		FinishDash(true);
		return;
	}

	// The same authoritative frame releases the hold pose and starts movement.
	StartExecutedPathPresentation();
	StopPathTelegraph();
	StopChargingCue();
	BeginDash();
}

void UAbilityTask_BossSlashDashExecution::BeginDash()
{
	if (Phase != EDashSlashPhase::DashAttacking || bDashStarted || bFinishing)
	{
		return;
	}

	AShipBossEnemy* Boss = GetBossAvatar();
	UCharacterMovementComponent* Movement = Boss ? Boss->GetCharacterMovement() : nullptr;
	UStaticMeshComponent* DeckMesh = CapturedDeckMesh.Get();
	FVector StartWorld;
	FVector EndWorld;
	FVector SurfaceNormal;
	if (!Boss || !Movement || !DeckMesh || CapturedDestinationLocation.NodeIndex == INDEX_NONE
		|| !ResolveCommittedPathWorld(StartWorld, EndWorld, SurfaceNormal))
	{
		EnemyPointSelectionFailure::Log(this, Boss, TEXT("Dash destination or committed path is no longer valid."));
		FinishDash(true);
		return;
	}

	const UDeckWalkAreaComponent* Area = Boss->GetHostShip()->GetDeckWalkAreaComponent();
	if (!Area || !Area->IsSupportedSegment(CapturedStartLocation, CapturedDestinationLocation)
		|| FVector::Dist(Boss->GetActorLocation(), StartWorld) > 50.0f)
	{
		EnemyPointSelectionFailure::Log(this, Boss, TEXT("Dash start or supported segment is no longer valid."));
		FinishDash(true);
		return;
	}

	// Movement is locked only during Windup. Restore the pre-ability movement
	// mode on the same frame as the DashSlash section so the slash animation and
	// authoritative dash translation advance together, as they did originally.
	RestoreMovementAfterAbility();
	bDashStarted = true;
	const FVector PathDirection = (EndWorld - StartWorld).GetSafeNormal();
	const FQuat StartRotation = PathDirection.IsNearlyZero()
		? Boss->GetActorQuat()
		: FRotationMatrix::MakeFromXZ(PathDirection, SurfaceNormal).ToQuat();
	Boss->SetActorLocationAndRotation(
		StartWorld, StartRotation, false, nullptr, ETeleportType::TeleportPhysics);
	if (AEnemyShip* Ship = Boss->GetHostShip(); Ship && Ship->GetDeckWalkAreaComponent())
		Movement->SetBase(Ship->GetDeckWalkAreaComponent()->GetMovementBase(*Boss));
	PreviousWorldLocation = StartWorld;
	DashStartServerTime = Boss->GetWorld()->GetTimeSeconds();
	ActivateDashCollision();
	if (!IsActive() || bFinishing) return;

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		auto Spec = ASC->MakeOutgoingSpec(UBossAbilityStateEffect::StaticClass(), 1.f, ASC->MakeEffectContext());
		if (Spec.IsValid())
		{
			Spec.Data->SetDuration(DashDuration + 0.25f, true);
			Spec.Data->DynamicGrantedTags.AddTag(State_Boss_Dashing);
			DashStateHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		}
	}

	FTimerManager& TimerManager = Boss->GetWorldTimerManager();
	TimerManager.SetTimer(
		DashTimerHandle,
		this,
		&UAbilityTask_BossSlashDashExecution::TickDash,
		DashTickInterval,
		true);
	TimerManager.SetTimer(
		SlashCompletionTimerHandle,
		this,
		&UAbilityTask_BossSlashDashExecution::MarkSlashFinished,
		GetSectionDurationSeconds(MontageConfig.AttackSectionName),
		false);
}

void UAbilityTask_BossSlashDashExecution::MarkSlashFinished()
{
	if (bSlashFinished || bFinishing || !bDashStarted)
	{
		return;
	}

	bSlashFinished = true;
	if (!bDestinationReached)
	{
		Phase = EDashSlashPhase::WaitingForCompletion;
	}
	TryStartRecovery();
}

void UAbilityTask_BossSlashDashExecution::TickDash()
{
	AShipBossEnemy* Boss = GetBossAvatar();
	UStaticMeshComponent* DeckMesh = CapturedDeckMesh.Get();
	UWorld* World = Boss ? Boss->GetWorld() : nullptr;
	if (!IsActive() || bFinishing || !Boss || !DeckMesh || !World)
	{
		FinishDash(true);
		return;
	}

	const double ElapsedSeconds = World->GetTimeSeconds() - DashStartServerTime;
	const float Alpha = FMath::Clamp(
		static_cast<float>(ElapsedSeconds) / FMath::Max(0.05f, DashDuration),
		0.0f,
		1.0f);
	FVector StartWorld;
	FVector EndWorld;
	FVector SurfaceNormal;
	if (!ResolveCommittedPathWorld(StartWorld, EndWorld, SurfaceNormal))
	{
		EnemyPointSelectionFailure::Log(this, Boss, TEXT("Committed dash points became invalid during movement."));
		FinishDash(true);
		return;
	}
	const FVector NewWorldLocation = FMath::Lerp(StartWorld, EndWorld, Alpha);
	const FVector MoveDirection = (NewWorldLocation - PreviousWorldLocation).GetSafeNormal();
	const FQuat NewRotation = MoveDirection.IsNearlyZero()
		? Boss->GetActorQuat()
		: FRotationMatrix::MakeFromXZ(MoveDirection, SurfaceNormal).ToQuat();

	Boss->SetActorLocationAndRotation(
		NewWorldLocation,
		NewRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	if (!IsActive() || bFinishing) return;
	if (USphereComponent* DamageVolume = Boss->GetDashDamageVolume())
	{
		DamageVolume->UpdateOverlaps();
	}
	if (!IsActive() || bFinishing) return;
	ApplySweptDashHits(PreviousWorldLocation, NewWorldLocation);
	if (!IsActive() || bFinishing) return;
	PreviousWorldLocation = NewWorldLocation;

	if (Alpha >= 1.0f - KINDA_SMALL_NUMBER)
	{
		HandleDestinationReached();
	}
}

void UAbilityTask_BossSlashDashExecution::ApplySweptDashHits(const FVector& SegmentStart, const FVector& SegmentEnd)
{
	AShipBossEnemy* Boss = GetBossAvatar();
	UWorld* World = Boss ? Boss->GetWorld() : nullptr;
	if (!Boss || !World)
	{
		return;
	}

	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BossDashDamage), false, Boss);
	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(
		Hits,
		SegmentStart,
		SegmentEnd,
		FQuat::Identity,
		ObjectQuery,
		FCollisionShape::MakeSphere(DashHitRadius),
		QueryParams);
	for (const FHitResult& Hit : Hits)
	{
		TryApplyDashDamage(Hit.GetActor(), Hit);
		if (!IsActive() || bFinishing) return;
	}
}

void UAbilityTask_BossSlashDashExecution::HandleDashOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	AShipBossEnemy* Boss = GetBossAvatar();
	if (!Boss || !OtherActor)
	{
		return;
	}

	FHitResult HitResult = SweepResult;
	if (!HitResult.GetActor())
	{
		const FVector ImpactPoint = OtherActor->GetActorLocation();
		const FVector ImpactNormal = (ImpactPoint - Boss->GetActorLocation()).GetSafeNormal();
		HitResult = FHitResult(OtherActor, OtherComponent, ImpactPoint, ImpactNormal);
	}
	TryApplyDashDamage(OtherActor, HitResult);
}

void UAbilityTask_BossSlashDashExecution::TryApplyDashDamage(AActor* Target, const FHitResult& HitResult)
{
	AShipBossEnemy* Boss = GetBossAvatar();
	if (!IsActive() || bFinishing || !bDashStarted || bDestinationReached || !Boss || !Boss->HasAuthority() || !Boss->CanEngageActor(Target))
	{
		return;
	}

	if (ShouldBroadcastAbilityTaskDelegates()) OnHit.Broadcast(Target, HitResult);
}

void UAbilityTask_BossSlashDashExecution::HandleDestinationReached()
{
	if (bDestinationReached || bFinishing)
	{
		return;
	}
	bDestinationReached = true;

	AShipBossEnemy* Boss = GetBossAvatar();
	UStaticMeshComponent* DeckMesh = CapturedDeckMesh.Get();
	if (!Boss || !DeckMesh || !Boss->HasDestination()
		|| Boss->GetDestinationLocation().NodeIndex != CapturedDestinationLocation.NodeIndex
		|| Boss->GetDestinationLocation().Revision != CapturedDestinationLocation.Revision)
	{
		EnemyPointSelectionFailure::Log(this, Boss, TEXT("DashSlash lost its captured walk-area destination."));
		FinishDash(true);
		return;
	}

	Boss->GetWorldTimerManager().ClearTimer(DashTimerHandle);
	if (UCharacterMovementComponent* Movement = Boss->GetCharacterMovement())
	{
		if (AEnemyShip* Ship = Boss->GetHostShip(); Ship && Ship->GetDeckWalkAreaComponent())
		Movement->SetBase(Ship->GetDeckWalkAreaComponent()->GetMovementBase(*Boss));
	}
	Boss->MarkDestinationReached();
	DeactivateDashCollision();
	ClearDashState();
	if (!bSlashFinished)
	{
		Phase = EDashSlashPhase::WaitingForCompletion;
	}
	TryStartRecovery();
}

void UAbilityTask_BossSlashDashExecution::TryStartRecovery()
{
	if (bSlashFinished && bDestinationReached)
	{
		// Preserve the complete slash and movement, but recover only after the final dash.
		if (bSkipRecovery) FinishDash(false);
		else StartRecovery();
	}
}

void UAbilityTask_BossSlashDashExecution::StartRecovery()
{
	if (Phase == EDashSlashPhase::Recovering || bFinishing
		|| !bSlashFinished || !bDestinationReached)
	{
		return;
	}
	AShipBossEnemy* Boss = GetBossAvatar();
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!Boss || !ASC)
	{
		FinishDash(true);
		return;
	}

	ASC->CurrentMontageSetNextSectionName(
		MontageConfig.AttackSectionName,
		MontageConfig.RecoverySectionName);
	ASC->CurrentMontageSetNextSectionName(
		MontageConfig.TravelHoldSectionName,
		MontageConfig.RecoverySectionName);
	if (!TransitionMontagePhase(
		EDashSlashPhase::WaitingForCompletion,
		EDashSlashPhase::Recovering,
		MontageConfig.RecoverySectionName))
	{
		FinishDash(true);
		return;
	}

	Boss->GetWorldTimerManager().SetTimer(
		RecoveryTimeoutTimerHandle,
		this,
		&UAbilityTask_BossSlashDashExecution::HandleRecoveryTimeout,
		MontageConfig.RecoveryTimeout,
		false);
}

void UAbilityTask_BossSlashDashExecution::ConfigureMontageSections()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	ASC->CurrentMontageSetNextSectionName(
		MontageConfig.WindupEnterSectionName,
		MontageConfig.WindupHoldSectionName);
	ASC->CurrentMontageSetNextSectionName(
		MontageConfig.WindupHoldSectionName,
		MontageConfig.WindupHoldSectionName);
	ASC->CurrentMontageSetNextSectionName(
		MontageConfig.AttackSectionName,
		MontageConfig.TravelHoldSectionName);
	ASC->CurrentMontageSetNextSectionName(
		MontageConfig.TravelHoldSectionName,
		MontageConfig.TravelHoldSectionName);
	ASC->CurrentMontageSetNextSectionName(
		MontageConfig.RecoverySectionName,
		NAME_None);
}

bool UAbilityTask_BossSlashDashExecution::TransitionMontagePhase(
	const EDashSlashPhase ExpectedPhase,
	const EDashSlashPhase NextPhase,
	const FName DestinationSection)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (bFinishing || Phase != ExpectedPhase || !ASC
		|| ASC->GetCurrentMontage() != MontageConfig.Montage
		|| !HasMontageSection(DestinationSection))
	{
		UE_LOG(LogBossSlashDashExecution, Warning,
			TEXT("DashSlash montage phase transition rejected. Boss=%s Phase=%d Expected=%d Section=%s CurrentMontage=%s"),
			*GetNameSafe(GetBossAvatar()), static_cast<uint8>(Phase),
			static_cast<uint8>(ExpectedPhase), *DestinationSection.ToString(),
			*GetNameSafe(ASC ? ASC->GetCurrentMontage() : nullptr));
		return false;
	}

	ASC->CurrentMontageJumpToSection(DestinationSection);
	Phase = NextPhase;
	return true;
}

bool UAbilityTask_BossSlashDashExecution::ValidateMontageConfig(FString& OutError) const
{
	OutError.Reset();
	if (!MontageConfig.Montage)
	{
		OutError = TEXT("MontageConfig.Montage is not assigned.");
		return false;
	}
	if (!FMath::IsFinite(MontageConfig.PlayRate) || MontageConfig.PlayRate <= 0.0f)
	{
		OutError = TEXT("MontageConfig.PlayRate must be greater than zero.");
		return false;
	}
	if (!FMath::IsFinite(MontageConfig.WindupHoldDuration) || !FMath::IsFinite(MontageConfig.RecoveryTimeout)
		|| MontageConfig.WindupHoldDuration < 0.0f || MontageConfig.RecoveryTimeout <= 0.0f)
	{
		OutError = TEXT("WindupHoldDuration and RecoveryTimeout are invalid.");
		return false;
	}

	const TArray<FName> ConfiguredSections = {
		MontageConfig.WindupEnterSectionName,
		MontageConfig.WindupHoldSectionName,
		MontageConfig.AttackSectionName,
		MontageConfig.TravelHoldSectionName,
		MontageConfig.RecoverySectionName
	};
	TSet<FName> UniqueSections;
	for (const FName SectionName : ConfiguredSections)
	{
		if (SectionName.IsNone() || UniqueSections.Contains(SectionName))
		{
			OutError = TEXT("All five Montage section names must be non-empty and unique.");
			return false;
		}
		UniqueSections.Add(SectionName);
	}

	for (const FName SectionName : ConfiguredSections)
	{
		if (!HasMontageSection(SectionName))
		{
			OutError = FString::Printf(
				TEXT("Montage '%s' is missing required section '%s'."),
				*GetNameSafe(MontageConfig.Montage),
				*SectionName.ToString());
			return false;
		}
		if (GetSectionDurationSeconds(SectionName) <= KINDA_SMALL_NUMBER)
		{
			OutError = FString::Printf(
				TEXT("Montage section '%s' must have a positive duration."),
				*SectionName.ToString());
			return false;
		}
	}
	return true;
}

bool UAbilityTask_BossSlashDashExecution::HasMontageSection(FName SectionName) const
{
	return MontageConfig.Montage && !SectionName.IsNone()
		&& MontageConfig.Montage->GetSectionIndex(SectionName) != INDEX_NONE;
}

float UAbilityTask_BossSlashDashExecution::GetSectionDurationSeconds(FName SectionName) const
{
	if (!MontageConfig.Montage || MontageConfig.PlayRate <= 0.0f)
	{
		return 0.0f;
	}
	const int32 SectionIndex = MontageConfig.Montage->GetSectionIndex(SectionName);
	return SectionIndex == INDEX_NONE
		? 0.0f
		: MontageConfig.Montage->GetSectionLength(SectionIndex) / MontageConfig.PlayRate;
}

void UAbilityTask_BossSlashDashExecution::ActivateDashCollision()
{
	AShipBossEnemy* Boss = GetBossAvatar();
	UCapsuleComponent* Capsule = Boss ? Boss->GetCapsuleComponent() : nullptr;
	USphereComponent* DamageVolume = Boss ? Boss->GetDashDamageVolume() : nullptr;
	if (!Boss || !Capsule || !DamageVolume)
	{
		return;
	}

	CachedPawnCollisionResponse = Capsule->GetCollisionResponseToChannel(ECC_Pawn);
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	bCollisionOverrideActive = true;
	DamageVolume->SetSphereRadius(FMath::Max(1.0f, DashHitRadius), true);
	DamageVolume->OnComponentBeginOverlap.AddUniqueDynamic(this, &UAbilityTask_BossSlashDashExecution::HandleDashOverlap);
	DamageVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DamageVolume->UpdateOverlaps();
}

void UAbilityTask_BossSlashDashExecution::DeactivateDashCollision()
{
	AShipBossEnemy* Boss = GetBossAvatar();
	if (USphereComponent* DamageVolume = Boss ? Boss->GetDashDamageVolume() : nullptr)
	{
		DamageVolume->OnComponentBeginOverlap.RemoveDynamic(this, &UAbilityTask_BossSlashDashExecution::HandleDashOverlap);
		DamageVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (bCollisionOverrideActive)
	{
		if (UCapsuleComponent* Capsule = Boss ? Boss->GetCapsuleComponent() : nullptr)
		{
			Capsule->SetCollisionResponseToChannel(ECC_Pawn, CachedPawnCollisionResponse);
		}
		bCollisionOverrideActive = false;
	}
}

void UAbilityTask_BossSlashDashExecution::HandleMontageCompleted()
{
	const bool bCompletedRecovery = Phase == EDashSlashPhase::Recovering
		&& bSlashFinished && bDestinationReached;
	if (!bCompletedRecovery)
	{
		UE_LOG(LogBossSlashDashExecution, Warning,
			TEXT("DashSlash montage completed before authoritative gameplay phases. Boss=%s"),
			*GetNameSafe(GetBossAvatar()));
	}
	FinishDash(!bCompletedRecovery);
}

void UAbilityTask_BossSlashDashExecution::HandleMontageBlendOut()
{
}

void UAbilityTask_BossSlashDashExecution::HandleMontageInterrupted()
{
	FinishDash(true);
}

void UAbilityTask_BossSlashDashExecution::HandleRecoveryTimeout()
{
	UE_LOG(LogBossSlashDashExecution, Warning,
		TEXT("Dash recovery montage timed out. Boss=%s Montage=%s"),
		*GetNameSafe(GetBossAvatar()), *GetNameSafe(MontageConfig.Montage));
	FinishDash(false);
}

bool UAbilityTask_BossSlashDashExecution::CapturePreselectedDestination()
{
	AShipBossEnemy* Boss = GetBossAvatar();
	AEnemyShip* HostShip = Boss ? Boss->GetHostShip() : nullptr;
	UStaticMeshComponent* DeckMesh = HostShip ? HostShip->GetShipDeckMesh() : nullptr;
	FTransform Destination;
	const UDeckWalkAreaComponent* Area = HostShip ? HostShip->GetDeckWalkAreaComponent() : nullptr;
	const FDeckWalkLocation Goal = Boss ? Boss->GetDestinationLocation() : FDeckWalkLocation();
	FDeckWalkLocation Start;
	if (!Boss || !Boss->HasAuthority() || !DeckMesh
		|| !Area || !Area->ResolveActorOnDeck(*Boss, Start) || !Area->IsSupportedSegment(Start, Goal)
		|| !Boss->ResolveDestinationTransform(Destination))
	{
		return false;
	}
	FVector LocalFeet = Area->ToLocal(Area->GetActorFeetWorld(*Boss));
	LocalFeet.Z = Start.LocalFloor.Z;
	FDeckWalkLocation PreciseStart;
	if (!Area->ResolvePreciseLocalFloor(LocalFeet, Start.SurfaceId, PreciseStart)
		|| !Area->IsSupportedSegment(PreciseStart, Goal)) return false;
	Start = PreciseStart;

	const FTransform ReferenceTransform = HostShip->GetActorTransform();
	const FVector SurfaceNormalWorld = DeckMesh->GetUpVector().GetSafeNormal();
	static uint32 NextInstanceId = 0;
	NextInstanceId = NextInstanceId % static_cast<uint32>(MAX_int32) + 1;
	NextPathInstanceId = static_cast<int32>(NextInstanceId);

	CapturedDeckMesh = DeckMesh;
	CapturedDestinationLocation = Goal;
	CapturedStartLocation = Start;
	CommittedPath.ReferenceActor = HostShip;
	CommittedPath.StartLocal = ReferenceTransform.InverseTransformPosition(Boss->GetActorLocation());
	CommittedPath.EndLocal = ReferenceTransform.InverseTransformPosition(Destination.GetLocation());
	CommittedPath.SurfaceNormalLocal = ReferenceTransform.InverseTransformVectorNoScale(
		SurfaceNormalWorld).GetSafeNormal();
	CommittedPath.CorridorRadius = FMath::Max(1.0f, DashHitRadius);
	CommittedPath.InstanceId = NextPathInstanceId;
	return true;
}

bool UAbilityTask_BossSlashDashExecution::ValidateCommittedPath(FString& OutError) const
{
	OutError.Reset();
	if (!CommittedPath.IsValid())
	{
		OutError = TEXT("Committed path payload is invalid.");
		return false;
	}

	const FVector Delta = FVector(CommittedPath.EndLocal) - FVector(CommittedPath.StartLocal);
	const FVector SurfaceNormal = FVector(CommittedPath.SurfaceNormalLocal).GetSafeNormal();
	const float PlanarDistance = FVector::VectorPlaneProject(Delta, SurfaceNormal).Size();
	if (PlanarDistance < FMath::Max(1.0f, MinimumDashDistance))
	{
		OutError = FString::Printf(
			TEXT("Selected path is too short (%.1f cm, minimum %.1f cm)."),
			PlanarDistance, MinimumDashDistance);
		return false;
	}
	return true;
}

FActiveGameplayEffectHandle UAbilityTask_BossSlashDashExecution::ApplyPathPresentationEffect(
	TSubclassOf<UGameplayEffect> EffectClass) const
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	AShipBossEnemy* Boss = GetBossAvatar();
	if (!ASC || !Boss || !Boss->HasAuthority() || !EffectClass || !CommittedPath.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}

	FGameplayEffectContextHandle Context = USWCombatEffectContextLibrary::MakeCombatEffectContext(
		ASC, Boss, Boss, nullptr);
	Context = USWCombatEffectContextLibrary::SetPathCuePayload(Context, CommittedPath);
	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		EffectClass, Ability->GetAbilityLevel(), Context);
	return Spec.IsValid() && Spec.Data.IsValid()
		? ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get())
		: FActiveGameplayEffectHandle();
}

void UAbilityTask_BossSlashDashExecution::StartPathTelegraph()
{
	StopPathTelegraph();
	TSubclassOf<UGameplayEffect> EffectClass = UBossDashSlashTelegraphEffect::StaticClass();
	if (PathPresentation && PathPresentation->GetTelegraphEffectClass())
	{
		EffectClass = PathPresentation->GetTelegraphEffectClass();
	}
	TelegraphEffectHandle = ApplyPathPresentationEffect(EffectClass);
}

void UAbilityTask_BossSlashDashExecution::StopPathTelegraph()
{
	if (TelegraphEffectHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->RemoveActiveGameplayEffect(TelegraphEffectHandle);
		}
		TelegraphEffectHandle.Invalidate();
	}
}

void UAbilityTask_BossSlashDashExecution::StartExecutedPathPresentation()
{
	TSubclassOf<UGameplayEffect> EffectClass = UBossDashSlashExecutionPathEffect::StaticClass();
	if (PathPresentation && PathPresentation->GetExecutionEffectClass())
	{
		EffectClass = PathPresentation->GetExecutionEffectClass();
	}
	ApplyPathPresentationEffect(EffectClass);
}

bool UAbilityTask_BossSlashDashExecution::LockMovementToCommittedStart()
{
	AShipBossEnemy* Boss = GetBossAvatar();
	UCharacterMovementComponent* Movement = Boss ? Boss->GetCharacterMovement() : nullptr;
	UStaticMeshComponent* DeckMesh = CapturedDeckMesh.Get();
	FVector StartWorld;
	FVector EndWorld;
	FVector SurfaceNormal;
	if (!Boss || !Movement || !DeckMesh
		|| !ResolveCommittedPathWorld(StartWorld, EndWorld, SurfaceNormal))
	{
		return false;
	}
	if (!Movement->IsMovingOnGround())
	{
		UE_LOG(LogBossSlashDashExecution, Warning,
			TEXT("Cannot lock DashSlash windup to a moving deck while the boss is not grounded. Boss=%s Mode=%d"),
			*GetNameSafe(Boss), static_cast<int32>(Movement->MovementMode));
		return false;
	}

	if (AAIController* Controller = Cast<AAIController>(Boss->GetController()))
	{
		Controller->StopMovement();
	}
	CachedMovementMode = Movement->MovementMode;
	CachedCustomMovementMode = Movement->CustomMovementMode;
	CachedMaxWalkSpeed = Movement->MaxWalkSpeed;
	Movement->StopMovementImmediately();
	Movement->SetMovementMode(MOVE_Walking);
	Movement->MaxWalkSpeed = 0.0f;
	Movement->bForceNextFloorCheck = true;
	bMovementLocked = true;

	const FVector Direction = (EndWorld - StartWorld).GetSafeNormal();
	const FQuat Rotation = Direction.IsNearlyZero()
		? Boss->GetActorQuat()
		: FRotationMatrix::MakeFromXZ(Direction, SurfaceNormal).ToQuat();
	Boss->SetActorRotation(Rotation, ETeleportType::TeleportPhysics);
	if (AEnemyShip* Ship = Boss->GetHostShip(); Ship && Ship->GetDeckWalkAreaComponent())
		Movement->SetBase(Ship->GetDeckWalkAreaComponent()->GetMovementBase(*Boss));
	Boss->ForceNetUpdate();
	return true;
}

void UAbilityTask_BossSlashDashExecution::RestoreMovementAfterAbility()
{
	if (!bMovementLocked)
	{
		return;
	}
	bMovementLocked = false;

	AShipBossEnemy* Boss = GetBossAvatar();
	UCharacterMovementComponent* Movement = Boss ? Boss->GetCharacterMovement() : nullptr;
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!Movement)
	{
		return;
	}
	Movement->StopMovementImmediately();
	Movement->MaxWalkSpeed = CachedMaxWalkSpeed;
	if (ASC && ASC->HasMatchingGameplayTag(State_Dead))
	{
		return;
	}
	Movement->SetMovementMode(CachedMovementMode, CachedCustomMovementMode);
	Movement->bForceNextFloorCheck = true;
	if (Boss)
	{
		Boss->ForceNetUpdate();
	}
}

bool UAbilityTask_BossSlashDashExecution::ResolveCommittedPathWorld(
	FVector& OutStart,
	FVector& OutEnd,
	FVector& OutSurfaceNormal) const
{
	AActor* ReferenceActor = CommittedPath.ReferenceActor.Get();
	const AShipBossEnemy* Boss = GetBossAvatar();
	const AEnemyShip* Ship = Boss ? Boss->GetHostShip() : nullptr;
	const UDeckWalkAreaComponent* Area = Ship ? Ship->GetDeckWalkAreaComponent() : nullptr;
	if (!CommittedPath.IsValid() || !IsValid(ReferenceActor) || ReferenceActor != Ship || !Area
		|| !Area->IsLocationValid(CapturedStartLocation) || !Area->IsLocationValid(CapturedDestinationLocation))
	{
		return false;
	}
	const FTransform& ReferenceTransform = ReferenceActor->GetActorTransform();
	OutStart = ReferenceTransform.TransformPosition(FVector(CommittedPath.StartLocal));
	OutEnd = ReferenceTransform.TransformPosition(FVector(CommittedPath.EndLocal));
	OutSurfaceNormal = ReferenceTransform.TransformVectorNoScale(
		FVector(CommittedPath.SurfaceNormalLocal)).GetSafeNormal();
	return !OutSurfaceNormal.IsNearlyZero();
}

void UAbilityTask_BossSlashDashExecution::FinishDash(bool bWasCancelled)
{
	if (!IsActive() || bFinishing) return;
	bFinishing = true;
	// Release every owned resource before the GA starts the next task.
	ClearRuntimeTimers();
	DeactivateDashCollision();
	ClearDashState();
	StopChargingCue();
	StopPathTelegraph();
	RestoreMovementAfterAbility();
	if (MontageTask)
	{
		MontageTask->OnCompleted.RemoveAll(this);
		MontageTask->OnBlendOut.RemoveAll(this);
		MontageTask->OnInterrupted.RemoveAll(this);
		MontageTask->OnCancelled.RemoveAll(this);
		if (auto* ASC = GetAbilitySystemComponentFromActorInfo(); ASC && ASC->GetAnimatingAbility() == Ability && ASC->GetCurrentMontage() == MontageConfig.Montage)
			ASC->CurrentMontageStop(bSkipRecovery ? 0.f : -1.f);
		MontageTask->EndTask();
		MontageTask = nullptr;
	}
	if (ShouldBroadcastAbilityTaskDelegates())
	{
		if (bWasCancelled) OnFailed.Broadcast();
		else OnCompleted.Broadcast();
	}
	EndTask();
}

void UAbilityTask_BossSlashDashExecution::ClearDashState()
{
	if (DashStateHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->RemoveActiveGameplayEffect(DashStateHandle);
		}
		DashStateHandle.Invalidate();
	}
}

void UAbilityTask_BossSlashDashExecution::ClearRuntimeTimers()
{
	if (AShipBossEnemy* Boss = GetBossAvatar())
	{
		FTimerManager& TimerManager = Boss->GetWorldTimerManager();
		TimerManager.ClearTimer(WindupLeadInTimerHandle);
		TimerManager.ClearTimer(WindupHoldTimerHandle);
		TimerManager.ClearTimer(SlashCompletionTimerHandle);
		TimerManager.ClearTimer(DashTimerHandle);
		TimerManager.ClearTimer(RecoveryTimeoutTimerHandle);
		Boss->GetWorldTimerManager().ClearTimer(WatchdogTimer);
	}
}

void UAbilityTask_BossSlashDashExecution::StartChargingCue()
{
	if (auto* ASC = GetAbilitySystemComponentFromActorInfo(); ASC && ChargingGameplayCueTag.IsValid() && BossAvatar.IsValid())
	{
		FGameplayCueParameters Parameters;
		Parameters.Instigator = BossAvatar.Get();
		Parameters.EffectCauser = BossAvatar.Get();
		bChargingCueActive = true;
		// Replicated persistent cue supports relevancy re-entry and late observers.
		ASC->AddGameplayCue(ChargingGameplayCueTag, Parameters);
	}
}

void UAbilityTask_BossSlashDashExecution::StopChargingCue()
{
	if (!bChargingCueActive) return;
	bChargingCueActive = false;
	if (auto* ASC = GetAbilitySystemComponentFromActorInfo()) ASC->RemoveGameplayCue(ChargingGameplayCueTag);
}

void UAbilityTask_BossSlashDashExecution::OnDestroy(bool bAbilityEnded)
{
	bFinishing = true;
	ClearRuntimeTimers();
	DeactivateDashCollision();
	ClearDashState();
	StopChargingCue();
	StopPathTelegraph();
	RestoreMovementAfterAbility();
	if (MontageTask)
	{
		MontageTask->OnCompleted.RemoveAll(this);
		MontageTask->OnBlendOut.RemoveAll(this);
		MontageTask->OnInterrupted.RemoveAll(this);
		MontageTask->OnCancelled.RemoveAll(this);
		if (auto* ASC = GetAbilitySystemComponentFromActorInfo(); ASC && ASC->GetAnimatingAbility() == Ability && ASC->GetCurrentMontage() == MontageConfig.Montage)
			ASC->CurrentMontageStop();
		MontageTask->EndTask();
		MontageTask = nullptr;
	}
	Super::OnDestroy(bAbilityEnded);
}
