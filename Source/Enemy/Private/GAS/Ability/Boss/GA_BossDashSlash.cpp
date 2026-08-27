#include "GAS/Ability/Boss/GA_BossDashSlash.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "BaseGameplayTags.h"
#include "BossAI/BossDeckMovementUtils.h"
#include "BossAI/ShipBossEnemy.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GASDamageInstantGameplayEffect.h"
#include "ShipAI/EnemyShip.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogBossDashSlash, Log, All);

UGA_BossDashSlash::UGA_BossDashSlash()
{
	SetBossAbilityTags(GameplayAbility_Boss_DashSlash, Cooldown_Boss_DashSlash);
	CooldownDuration = 5.0f;
	DamageEffectClass = UGASDamageInstantGameplayEffect::StaticClass();
	ImpactGameplayCueTag = GameplayCue_Impact_Boss_DashSlash;
}

void UGA_BossDashSlash::PostLoad()
{
	Super::PostLoad();

	// Preserve values serialized by the pre-MontageConfig BPGA_SlashDash class.
	if (!MontageConfig.Montage && DashMontage_DEPRECATED)
	{
		MontageConfig.Montage = DashMontage_DEPRECATED;
		MontageConfig.WindupEnterSectionName = WindupSectionName_DEPRECATED;
		MontageConfig.AttackSectionName = DashSlashSectionName_DEPRECATED;
		MontageConfig.TravelHoldSectionName = DashHoldSectionName_DEPRECATED;
		MontageConfig.RecoverySectionName = RecoverySectionName_DEPRECATED;
		MontageConfig.WindupHoldDuration = WindupDuration_DEPRECATED;
		MontageConfig.RecoveryTimeout = RecoveryTimeout_DEPRECATED;
	}
}

void UGA_BossDashSlash::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	Phase = EDashSlashPhase::Inactive;
	bDashStarted = false;
	bSlashFinished = false;
	bDestinationReached = false;
	bFinishing = false;
	HitActorsThisDash.Reset();
	CapturedDeckMesh.Reset();
	CapturedDestinationPointId = INDEX_NONE;
	DashEndLocal = FVector::ZeroVector;

	FString MontageError;
	if (!CapturePreselectedDestination() || !ValidateMontageConfig(MontageError))
	{
		if (!MontageError.IsEmpty())
		{
			UE_LOG(LogBossDashSlash, Error, TEXT("Cannot activate DashSlash: %s"), *MontageError);
		}
		else
		{
			UE_LOG(LogBossDashSlash, Warning,
				TEXT("Cannot activate DashSlash: preselected destination capture failed. Boss=%s"),
				*GetNameSafe(GetBossAvatar()));
		}
		FinishDash(true);
		return;
	}
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		FinishDash(true);
		return;
	}

	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this,
		TEXT("BossDashSlashMontage"),
		MontageConfig.Montage,
		MontageConfig.PlayRate,
		MontageConfig.WindupEnterSectionName,
		true);
	if (!MontageTask)
	{
		FinishDash(true);
		return;
	}

	MontageTask->OnCompleted.AddDynamic(this, &UGA_BossDashSlash::HandleMontageCompleted);
	MontageTask->OnBlendOut.AddDynamic(this, &UGA_BossDashSlash::HandleMontageBlendOut);
	MontageTask->OnInterrupted.AddDynamic(this, &UGA_BossDashSlash::HandleMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &UGA_BossDashSlash::HandleMontageInterrupted);
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
			&UGA_BossDashSlash::BeginWindupHold,
			LeadInDuration,
			false);
	}
}

void UGA_BossDashSlash::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (AShipBossEnemy* Boss = GetBossAvatar())
	{
		ClearRuntimeTimers();
		if (bWasCancelled)
		{
			Boss->SetDestinationPointId(INDEX_NONE);
		}
	}
	DeactivateDashCollision();
	ClearDashState();
	MontageTask = nullptr;
	DashStartServerTime = 0.0;
	EffectiveDashAcceptanceRadius = 0.0f;
	HitActorsThisDash.Reset();
	CapturedDeckMesh.Reset();
	CapturedDestinationPointId = INDEX_NONE;
	DashStartLocal = FVector::ZeroVector;
	DashEndLocal = FVector::ZeroVector;
	PreviousWorldLocation = FVector::ZeroVector;
	Phase = EDashSlashPhase::Inactive;
	bDashStarted = false;
	bSlashFinished = false;
	bDestinationReached = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_BossDashSlash::BeginWindupHold()
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
	if (MontageConfig.WindupHoldDuration <= KINDA_SMALL_NUMBER)
	{
		ReleaseWindupAndBeginDash();
		return;
	}

	Boss->GetWorldTimerManager().SetTimer(
		WindupHoldTimerHandle,
		this,
		&UGA_BossDashSlash::ReleaseWindupAndBeginDash,
		MontageConfig.WindupHoldDuration,
		false);
}

void UGA_BossDashSlash::ReleaseWindupAndBeginDash()
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
	BeginDash();
}

void UGA_BossDashSlash::BeginDash()
{
	if (Phase != EDashSlashPhase::DashAttacking || bDashStarted || bFinishing)
	{
		return;
	}

	AShipBossEnemy* Boss = GetBossAvatar();
	UStaticMeshComponent* DeckMesh = CapturedDeckMesh.Get();
	if (!Boss || !DeckMesh || CapturedDestinationPointId == INDEX_NONE)
	{
		FinishDash(true);
		return;
	}

	bDashStarted = true;
	const FTransform DeckTransform = DeckMesh->GetComponentTransform();
	DashStartLocal = DeckTransform.InverseTransformPosition(Boss->GetActorLocation());
	const float DashTravelDistance = FVector2D::Distance(
		FVector2D(DashStartLocal.X, DashStartLocal.Y),
		FVector2D(DashEndLocal.X, DashEndLocal.Y));
	EffectiveDashAcceptanceRadius = BossDeckMovement::ResolveAcceptanceRadius(
		DashAcceptanceRadius, DashTravelDistance);
	PreviousWorldLocation = Boss->GetActorLocation();
	DashStartServerTime = Boss->GetWorld()->GetTimeSeconds();
	HitActorsThisDash.Reset();
	ActivateDashCollision();

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		DashStateHandle = ApplyTimedStateTag(*ASC, State_Boss_Dashing, DashDuration + 0.25f);
	}

	FTimerManager& TimerManager = Boss->GetWorldTimerManager();
	TimerManager.SetTimer(
		DashTimerHandle,
		this,
		&UGA_BossDashSlash::TickDash,
		DashTickInterval,
		true);
	TimerManager.SetTimer(
		SlashCompletionTimerHandle,
		this,
		&UGA_BossDashSlash::MarkSlashFinished,
		GetSectionDurationSeconds(MontageConfig.AttackSectionName),
		false);
}

void UGA_BossDashSlash::MarkSlashFinished()
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

void UGA_BossDashSlash::TickDash()
{
	AShipBossEnemy* Boss = GetBossAvatar();
	UStaticMeshComponent* DeckMesh = CapturedDeckMesh.Get();
	UWorld* World = Boss ? Boss->GetWorld() : nullptr;
	if (!Boss || !DeckMesh || !World)
	{
		FinishDash(true);
		return;
	}

	const double ElapsedSeconds = World->GetTimeSeconds() - DashStartServerTime;
	const float Alpha = FMath::Clamp(
		static_cast<float>(ElapsedSeconds) / FMath::Max(0.05f, DashDuration),
		0.0f,
		1.0f);
	const FVector NewWorldLocation = DeckMesh->GetComponentTransform().TransformPosition(
		FMath::Lerp(DashStartLocal, DashEndLocal, Alpha));
	const FVector MoveDirection = (NewWorldLocation - PreviousWorldLocation).GetSafeNormal();
	const FQuat NewRotation = MoveDirection.IsNearlyZero()
		? Boss->GetActorQuat()
		: FRotationMatrix::MakeFromXZ(MoveDirection, DeckMesh->GetUpVector()).ToQuat();

	Boss->SetActorLocationAndRotation(
		NewWorldLocation,
		NewRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	if (USphereComponent* DamageVolume = Boss->GetDashDamageVolume())
	{
		DamageVolume->UpdateOverlaps();
	}
	ApplySweptDashHits(PreviousWorldLocation, NewWorldLocation);
	PreviousWorldLocation = NewWorldLocation;

	const FVector NewLocalLocation = DeckMesh->GetComponentTransform().InverseTransformPosition(
		NewWorldLocation);
	if (BossDeckMovement::IsWithinPlanarAcceptance(
		NewLocalLocation, DashEndLocal, EffectiveDashAcceptanceRadius)
		|| Alpha >= 1.0f - KINDA_SMALL_NUMBER)
	{
		HandleDestinationReached();
	}
}

void UGA_BossDashSlash::ApplySweptDashHits(const FVector& SegmentStart, const FVector& SegmentEnd)
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
	}
}

void UGA_BossDashSlash::HandleDashOverlap(
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

void UGA_BossDashSlash::TryApplyDashDamage(AActor* Target, const FHitResult& HitResult)
{
	AShipBossEnemy* Boss = GetBossAvatar();
	if (!Boss || !Boss->HasAuthority() || !Boss->CanEngageActor(Target)
		|| HitActorsThisDash.Contains(Target))
	{
		return;
	}

	if (ApplyDamageToTarget(Target, DamageEffectClass, Damage, &HitResult))
	{
		HitActorsThisDash.Add(Target);
	}
}

void UGA_BossDashSlash::HandleDestinationReached()
{
	if (bDestinationReached || bFinishing)
	{
		return;
	}
	bDestinationReached = true;

	AShipBossEnemy* Boss = GetBossAvatar();
	UStaticMeshComponent* DeckMesh = CapturedDeckMesh.Get();
	if (!Boss || !DeckMesh
		|| Boss->GetDestinationPointId() != CapturedDestinationPointId)
	{
		UE_LOG(LogBossDashSlash, Warning,
			TEXT("DashSlash lost its captured destination before arrival. Boss=%s CapturedPoint=%d CurrentPoint=%d"),
			*GetNameSafe(Boss), CapturedDestinationPointId,
			Boss ? Boss->GetDestinationPointId() : INDEX_NONE);
		FinishDash(true);
		return;
	}

	Boss->GetWorldTimerManager().ClearTimer(DashTimerHandle);
	Boss->SetBase(DeckMesh);
	Boss->MarkDestinationReached();
	DeactivateDashCollision();
	ClearDashState();
	if (!bSlashFinished)
	{
		Phase = EDashSlashPhase::WaitingForCompletion;
	}
	TryStartRecovery();
}

void UGA_BossDashSlash::TryStartRecovery()
{
	if (bSlashFinished && bDestinationReached)
	{
		StartRecovery();
	}
}

void UGA_BossDashSlash::StartRecovery()
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
		&UGA_BossDashSlash::HandleRecoveryTimeout,
		MontageConfig.RecoveryTimeout,
		false);
}

void UGA_BossDashSlash::ConfigureMontageSections()
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

bool UGA_BossDashSlash::TransitionMontagePhase(
	const EDashSlashPhase ExpectedPhase,
	const EDashSlashPhase NextPhase,
	const FName DestinationSection)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (bFinishing || Phase != ExpectedPhase || !ASC
		|| ASC->GetCurrentMontage() != MontageConfig.Montage
		|| !HasMontageSection(DestinationSection))
	{
		UE_LOG(LogBossDashSlash, Warning,
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

bool UGA_BossDashSlash::ValidateMontageConfig(FString& OutError) const
{
	OutError.Reset();
	if (!MontageConfig.Montage)
	{
		OutError = TEXT("MontageConfig.Montage is not assigned.");
		return false;
	}
	if (MontageConfig.PlayRate <= 0.0f)
	{
		OutError = TEXT("MontageConfig.PlayRate must be greater than zero.");
		return false;
	}
	if (MontageConfig.WindupHoldDuration < 0.0f || MontageConfig.RecoveryTimeout <= 0.0f)
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

bool UGA_BossDashSlash::HasMontageSection(FName SectionName) const
{
	return MontageConfig.Montage && !SectionName.IsNone()
		&& MontageConfig.Montage->GetSectionIndex(SectionName) != INDEX_NONE;
}

float UGA_BossDashSlash::GetSectionDurationSeconds(FName SectionName) const
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

void UGA_BossDashSlash::ActivateDashCollision()
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
	DamageVolume->OnComponentBeginOverlap.AddUniqueDynamic(this, &UGA_BossDashSlash::HandleDashOverlap);
	DamageVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DamageVolume->UpdateOverlaps();
}

void UGA_BossDashSlash::DeactivateDashCollision()
{
	AShipBossEnemy* Boss = GetBossAvatar();
	if (USphereComponent* DamageVolume = Boss ? Boss->GetDashDamageVolume() : nullptr)
	{
		DamageVolume->OnComponentBeginOverlap.RemoveDynamic(this, &UGA_BossDashSlash::HandleDashOverlap);
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

void UGA_BossDashSlash::HandleMontageCompleted()
{
	const bool bCompletedRecovery = Phase == EDashSlashPhase::Recovering
		&& bSlashFinished && bDestinationReached;
	if (!bCompletedRecovery)
	{
		UE_LOG(LogBossDashSlash, Warning,
			TEXT("DashSlash montage completed before authoritative gameplay phases. Boss=%s"),
			*GetNameSafe(GetBossAvatar()));
	}
	FinishDash(!bCompletedRecovery);
}

void UGA_BossDashSlash::HandleMontageBlendOut()
{
}

void UGA_BossDashSlash::HandleMontageInterrupted()
{
	FinishDash(true);
}

void UGA_BossDashSlash::HandleRecoveryTimeout()
{
	UE_LOG(LogBossDashSlash, Warning,
		TEXT("Dash recovery montage timed out. Boss=%s Montage=%s"),
		*GetNameSafe(GetBossAvatar()), *GetNameSafe(MontageConfig.Montage));
	FinishDash(false);
}

bool UGA_BossDashSlash::CapturePreselectedDestination()
{
	AShipBossEnemy* Boss = GetBossAvatar();
	AActor* Target = GetBossTarget();
	AEnemyShip* HostShip = Boss ? Boss->GetHostShip() : nullptr;
	UStaticMeshComponent* DeckMesh = HostShip ? HostShip->GetShipDeckMesh() : nullptr;
	FTransform Destination;
	const int32 DestinationPointId = Boss ? Boss->GetDestinationPointId() : INDEX_NONE;
	if (!Boss || !Boss->HasAuthority() || !Boss->CanEngageActor(Target) || !DeckMesh
		|| DestinationPointId == INDEX_NONE
		|| !Boss->ResolvePointTransform(DestinationPointId, Destination))
	{
		return false;
	}

	CapturedDeckMesh = DeckMesh;
	CapturedDestinationPointId = DestinationPointId;
	DashEndLocal = DeckMesh->GetComponentTransform().InverseTransformPosition(
		Destination.GetLocation());
	return true;
}

void UGA_BossDashSlash::FinishDash(bool bWasCancelled)
{
	if (IsActive() && !bFinishing)
	{
		bFinishing = true;
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bWasCancelled);
	}
}

void UGA_BossDashSlash::ClearDashState()
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

void UGA_BossDashSlash::ClearRuntimeTimers()
{
	if (AShipBossEnemy* Boss = GetBossAvatar())
	{
		FTimerManager& TimerManager = Boss->GetWorldTimerManager();
		TimerManager.ClearTimer(WindupLeadInTimerHandle);
		TimerManager.ClearTimer(WindupHoldTimerHandle);
		TimerManager.ClearTimer(SlashCompletionTimerHandle);
		TimerManager.ClearTimer(DashTimerHandle);
		TimerManager.ClearTimer(RecoveryTimeoutTimerHandle);
	}
}
