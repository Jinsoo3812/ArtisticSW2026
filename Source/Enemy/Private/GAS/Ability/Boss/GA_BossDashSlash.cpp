#include "GAS/Ability/Boss/GA_BossDashSlash.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "BaseGameplayTags.h"
#include "BossAI/ShipBossEnemy.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
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

void UGA_BossDashSlash::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	bDashStarted = false;
	bSlashFinished = false;
	bDestinationReached = false;
	bRecoveryStarted = false;
	bFinishing = false;
	HitActorsThisDash.Reset();
	if (!ValidatePreselectedDestination() || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		FinishDash(true);
		return;
	}
	DashStartEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		Event_Boss_Dash_Start,
		nullptr,
		true,
		true);
	if (DashStartEventTask)
	{
		DashStartEventTask->EventReceived.AddDynamic(this, &UGA_BossDashSlash::HandleDashStartEvent);
		DashStartEventTask->ReadyForActivation();
	}
	SlashFinishedEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		Event_Boss_Dash_SlashFinished,
		nullptr,
		true,
		true);
	if (SlashFinishedEventTask)
	{
		SlashFinishedEventTask->EventReceived.AddDynamic(
			this, &UGA_BossDashSlash::HandleSlashFinishedEvent);
		SlashFinishedEventTask->ReadyForActivation();
	}

	if (DashMontage)
	{
		const FName StartSection = HasMontageSection(WindupSectionName)
			? WindupSectionName
			: NAME_None;
		MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("BossDashSlashMontage"),
			DashMontage,
			1.0f,
			StartSection,
			true);
		if (MontageTask)
		{
			MontageTask->OnCompleted.AddDynamic(this, &UGA_BossDashSlash::HandleMontageCompleted);
			MontageTask->OnBlendOut.AddDynamic(this, &UGA_BossDashSlash::HandleMontageBlendOut);
			MontageTask->OnInterrupted.AddDynamic(this, &UGA_BossDashSlash::HandleMontageInterrupted);
			MontageTask->OnCancelled.AddDynamic(this, &UGA_BossDashSlash::HandleMontageInterrupted);
			MontageTask->ReadyForActivation();
			ConfigureMontageSections();
		}
	}

	if (WindupDuration <= 0.0f)
	{
		BeginDash();
		return;
	}
	WindupTask = UAbilityTask_WaitDelay::WaitDelay(this, WindupDuration);
	WindupTask->OnFinish.AddDynamic(this, &UGA_BossDashSlash::BeginDash);
	WindupTask->ReadyForActivation();
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
		Boss->GetWorldTimerManager().ClearTimer(DashTimerHandle);
		if (bWasCancelled)
		{
			Boss->SetDestinationPointId(INDEX_NONE);
		}
	}
	DeactivateDashCollision();
	ClearDashState();
	if (DashStartEventTask)
	{
		DashStartEventTask->EndTask();
	}
	if (SlashFinishedEventTask)
	{
		SlashFinishedEventTask->EndTask();
	}
	if (WindupTask)
	{
		WindupTask->EndTask();
	}
	if (RecoveryTimeoutTask)
	{
		RecoveryTimeoutTask->EndTask();
	}
	DashStartEventTask = nullptr;
	SlashFinishedEventTask = nullptr;
	WindupTask = nullptr;
	RecoveryTimeoutTask = nullptr;
	MontageTask = nullptr;
	DashElapsed = 0.0f;
	HitActorsThisDash.Reset();
	bDashStarted = false;
	bSlashFinished = false;
	bDestinationReached = false;
	bRecoveryStarted = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_BossDashSlash::HandleDashStartEvent(FGameplayEventData Payload)
{
	BeginDash();
}

void UGA_BossDashSlash::HandleSlashFinishedEvent(FGameplayEventData Payload)
{
	if (bSlashFinished || bFinishing)
	{
		return;
	}

	bSlashFinished = true;
	if (SlashFinishedEventTask)
	{
		SlashFinishedEventTask->EndTask();
		SlashFinishedEventTask = nullptr;
	}

	if (bDestinationReached)
	{
		StartRecovery();
	}
}

void UGA_BossDashSlash::BeginDash()
{
	if (bDashStarted || bDestinationReached || bFinishing)
	{
		return;
	}
	bDashStarted = true;
	if (WindupTask)
	{
		WindupTask->EndTask();
		WindupTask = nullptr;
	}
	if (DashStartEventTask)
	{
		DashStartEventTask->EndTask();
		DashStartEventTask = nullptr;
	}

	AShipBossEnemy* Boss = GetBossAvatar();
	AEnemyShip* HostShip = Boss ? Boss->GetHostShip() : nullptr;
	UStaticMeshComponent* DeckMesh = HostShip ? HostShip->GetShipDeckMesh() : nullptr;
	FTransform Destination;
	if (!Boss || !DeckMesh || !Boss->ResolvePointTransform(Boss->GetDestinationPointId(), Destination))
	{
		FinishDash(true);
		return;
	}

	const FTransform DeckTransform = DeckMesh->GetComponentTransform();
	DashStartLocal = DeckTransform.InverseTransformPosition(Boss->GetActorLocation());
	DashEndLocal = DeckTransform.InverseTransformPosition(Destination.GetLocation());
	PreviousWorldLocation = Boss->GetActorLocation();
	DashElapsed = 0.0f;
	HitActorsThisDash.Reset();
	ActivateDashCollision();

	if (MontageTask && HasMontageSection(DashSlashSectionName))
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->CurrentMontageJumpToSection(DashSlashSectionName);
		}
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		DashStateHandle = ApplyTimedStateTag(*ASC, State_Boss_Dashing, DashDuration + 0.25f);
	}
	Boss->GetWorldTimerManager().SetTimer(
		DashTimerHandle,
		this,
		&UGA_BossDashSlash::TickDash,
		DashTickInterval,
		true);
}

void UGA_BossDashSlash::TickDash()
{
	AShipBossEnemy* Boss = GetBossAvatar();
	AEnemyShip* HostShip = Boss ? Boss->GetHostShip() : nullptr;
	UStaticMeshComponent* DeckMesh = HostShip ? HostShip->GetShipDeckMesh() : nullptr;
	if (!Boss || !DeckMesh)
	{
		FinishDash(true);
		return;
	}

	DashElapsed += DashTickInterval;
	const float Alpha = FMath::Clamp(DashElapsed / FMath::Max(0.05f, DashDuration), 0.0f, 1.0f);
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

	if (Alpha >= 1.0f - KINDA_SMALL_NUMBER)
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
	AEnemyShip* HostShip = Boss ? Boss->GetHostShip() : nullptr;
	UStaticMeshComponent* DeckMesh = HostShip ? HostShip->GetShipDeckMesh() : nullptr;
	if (!Boss || !DeckMesh)
	{
		FinishDash(true);
		return;
	}

	Boss->GetWorldTimerManager().ClearTimer(DashTimerHandle);
	Boss->SetBase(DeckMesh);
	Boss->MarkDestinationReached();
	DeactivateDashCollision();
	ClearDashState();

	if (MontageTask && HasMontageSection(RecoverySectionName))
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			// Arrival stops movement and damage immediately, but never truncates
			// the one-shot sword swing. A completed swing may recover at once.
			ASC->CurrentMontageSetNextSectionName(DashSlashSectionName, RecoverySectionName);
			ASC->CurrentMontageSetNextSectionName(DashHoldSectionName, RecoverySectionName);
		}
		if (bSlashFinished)
		{
			StartRecovery();
		}
		return;
	}
	FinishDash(false);
}

void UGA_BossDashSlash::StartRecovery()
{
	if (bRecoveryStarted || bFinishing || !bDestinationReached)
	{
		return;
	}
	bRecoveryStarted = true;

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->CurrentMontageSetNextSectionName(DashHoldSectionName, RecoverySectionName);
		ASC->CurrentMontageJumpToSection(RecoverySectionName);
	}

	RecoveryTimeoutTask = UAbilityTask_WaitDelay::WaitDelay(
		this, FMath::Max(0.1f, RecoveryTimeout));
	if (RecoveryTimeoutTask)
	{
		RecoveryTimeoutTask->OnFinish.AddDynamic(this, &UGA_BossDashSlash::HandleRecoveryTimeout);
		RecoveryTimeoutTask->ReadyForActivation();
	}
}

void UGA_BossDashSlash::ConfigureMontageSections()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC || !DashMontage)
	{
		return;
	}

	const bool bHasWindup = HasMontageSection(WindupSectionName);
	const bool bHasSlash = HasMontageSection(DashSlashSectionName);
	const bool bHasHold = HasMontageSection(DashHoldSectionName);
	const bool bHasRecovery = HasMontageSection(RecoverySectionName);
	if (!bHasWindup || !bHasSlash || !bHasHold || !bHasRecovery)
	{
		UE_LOG(LogBossDashSlash, Warning,
			TEXT("Dash montage %s should contain sections '%s', '%s', '%s', and '%s'. Using timing fallbacks for missing sections."),
			*GetNameSafe(DashMontage), *WindupSectionName.ToString(),
			*DashSlashSectionName.ToString(), *DashHoldSectionName.ToString(),
			*RecoverySectionName.ToString());
	}
	if (bHasWindup && bHasSlash)
	{
		ASC->CurrentMontageSetNextSectionName(WindupSectionName, DashSlashSectionName);
	}
	if (bHasSlash && bHasHold)
	{
		ASC->CurrentMontageSetNextSectionName(DashSlashSectionName, DashHoldSectionName);
	}
	if (bHasHold)
	{
		ASC->CurrentMontageSetNextSectionName(DashHoldSectionName, DashHoldSectionName);
	}
}

bool UGA_BossDashSlash::HasMontageSection(FName SectionName) const
{
	return DashMontage && !SectionName.IsNone()
		&& DashMontage->GetSectionIndex(SectionName) != INDEX_NONE;
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
	FinishDash(!bDestinationReached);
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
		*GetNameSafe(GetBossAvatar()), *GetNameSafe(DashMontage));
	FinishDash(false);
}

bool UGA_BossDashSlash::ValidatePreselectedDestination() const
{
	const AShipBossEnemy* Boss = GetBossAvatar();
	AActor* Target = GetBossTarget();
	FTransform Destination;
	if (!Boss || !Boss->CanEngageActor(Target) || !Boss->GetHostShip()
		|| Boss->GetDestinationPointId() == INDEX_NONE
		|| !Boss->ResolvePointTransform(Boss->GetDestinationPointId(), Destination))
	{
		return false;
	}
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
