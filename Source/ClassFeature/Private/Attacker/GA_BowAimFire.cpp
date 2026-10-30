#include "Attacker/GA_BowAimFire.h"
#include "Equipment/WeaponDefinition.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "Animation/AnimInstance.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Combat/PlayerAimComponent.h"
#include "Combat/PlayerBowShotPreparation.h"
#include "Item/Projectiles/PlayerArrowProjectile.h"
#include "Equipment/PlayerEquipmentComponent.h"
#include "Equipment/WeaponAnimationDataAsset.h"
#include "Item/Components/BowComponent.h"
#include "Item/Projectiles/ArrowProjectile.h"
#include "Item/Projectiles/ArrowCollisionQuery.h"
#include "Item/Weapons/BowItem.h"
#include "GASCombatLibrary.h"

UGA_BowAimFire::UGA_BowAimFire()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(GameplayAbility_Weapon_AimCycle);
	AssetTags.AddTag(GameplayAbility_InterruptibleByHit);
	SetAssetTags(AssetTags);
}

void UGA_BowAimFire::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bEndingAim = false;
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsAimSessionActive()) return;

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!IsAimSessionActive()) return;
	if (!CacheBowFromAvatar())
	{
		UE_LOG(LogTemp, Warning, TEXT("UGA_BowAimFire::ActivateAbility : Equipped item is not a valid bow."));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		bOwnsAimTag = true;
		ASC->AddLooseGameplayTag(State_Aiming);
	}

	if (!IsAimSessionActive()) return;
	RemoveBowStateTags();
	CachedBowComponent->SetAiming(true);
	CachedBowComponent->SetDrawAlpha(0.0f);
	CachedBowComponent->SetArrowNocked(false);
	if (ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo()))
	{
		if (Player->IsLocallyControlled() && Player->GetAimComponent())
			Player->GetAimComponent()->SetObstructionQuery(FPlayerAimObstructionQuery::CreateUObject(this, &UGA_BowAimFire::IsAimPathObstructed));
	}

	UAbilityTask_WaitInputRelease* WaitRightReleaseTask = UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
	if (WaitRightReleaseTask)
	{
		WaitRightReleaseTask->OnRelease.AddDynamic(this, &UGA_BowAimFire::OnRightClickReleased);
		AimInputTasks.Add(WaitRightReleaseTask);
		WaitRightReleaseTask->ReadyForActivation();
		if (!IsAimSessionActive()) return;
	}

	UAbilityTask_WaitGameplayEvent* WaitLeftPressedTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Key_Default_Mouse_LeftClick, nullptr, false, true);
	if (WaitLeftPressedTask)
	{
		WaitLeftPressedTask->EventReceived.AddDynamic(this, &UGA_BowAimFire::OnLeftClickPressed);
		AimInputTasks.Add(WaitLeftPressedTask);
		WaitLeftPressedTask->ReadyForActivation();
		if (!IsAimSessionActive()) return;
	}

	UAbilityTask_WaitGameplayEvent* WaitLeftReleasedTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Key_Default_Mouse_LeftClick_Released, nullptr, false, true);
	if (WaitLeftReleasedTask)
	{
		WaitLeftReleasedTask->EventReceived.AddDynamic(this, &UGA_BowAimFire::OnLeftClickReleased);
		AimInputTasks.Add(WaitLeftReleasedTask);
		WaitLeftReleasedTask->ReadyForActivation();
		if (!IsAimSessionActive()) return;
	}

	UAbilityTask_WaitGameplayEvent* WaitFireArrowTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Event_Montage_FireArrow, nullptr, false, true);
	if (WaitFireArrowTask)
	{
		WaitFireArrowTask->EventReceived.AddDynamic(this, &UGA_BowAimFire::OnReleaseFireEvent);
		AimInputTasks.Add(WaitFireArrowTask);
		WaitFireArrowTask->ReadyForActivation();
		if (!IsAimSessionActive()) return;
	}

	UAbilityTask_WaitGameplayEvent* WaitNockArrowTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Event_Montage_NockArrow, nullptr, false, true);
	if (WaitNockArrowTask)
	{
		WaitNockArrowTask->EventReceived.AddDynamic(this, &UGA_BowAimFire::OnNockArrowEvent);
		AimInputTasks.Add(WaitNockArrowTask);
		WaitNockArrowTask->ReadyForActivation();
		if (!IsAimSessionActive()) return;
	}
}

void UGA_BowAimFire::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (bEndingAim || !IsEndAbilityValid(Handle, ActorInfo)) return;
	if (ScopeLockCount > 0)
	{
		WaitingToExecute.Add(FPostLockDelegate::CreateUObject(this, &UGA_BowAimFire::EndAbility,
			Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled));
		return;
	}
	bEndingAim = true;
	for (UAbilityTask* Task : AimInputTasks)
	{
		if (auto* Event = Cast<UAbilityTask_WaitGameplayEvent>(Task)) Event->EventReceived.RemoveAll(this);
		if (auto* Release = Cast<UAbilityTask_WaitInputRelease>(Task)) Release->OnRelease.RemoveAll(this);
		if (Task) Task->EndTask();
	}
	AimInputTasks.Reset();
	if (bOwnsAimTag)
	{
		if (ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo()))
			if (Player->GetAimComponent()) Player->GetAimComponent()->SetObstructionQuery(FPlayerAimObstructionQuery());
	}
	ResetBowState();
	if (bOwnsAimTag)
	{
		bOwnsAimTag = false;
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo()) ASC->RemoveLooseGameplayTag(State_Aiming);
	}
	CachedBow = nullptr;
	CachedBowComponent = nullptr;
	ShotComponent.Reset();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_BowAimFire::OnLeftClickPressed(FGameplayEventData Payload)
{
	if (!IsAimSessionActive() || bFinishingShot || !CachedBowComponent || bIsDrawing || bIsFullyDrawn)
	{
		return;
	}

	if (bIsReleaseInProgress)
	{
		if (bHasFiredCurrentShot)
		{
			// The previous arrow already launched; cancel release recoil and immediately start drawing the next arrow
			FinishShot();
		}
		else
		{
			// Arrow is still waiting to fire from release notify
			return;
		}
	}

	const ABasePlayer* AvatarPlayer = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	FTransform ArrowSpawnTransform;
	if (AvatarPlayer && AvatarPlayer->HasAuthority()
		&& !CachedBow->TryGetArrowSpawnTransform(ArrowSpawnTransform))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("UGA_BowAimFire::OnLeftClickPressed: Bow %s is missing required socket %s."),
			*GetNameSafe(CachedBow),
			CachedBow ? *CachedBow->GetCharacterArrowSocketName().ToString() : TEXT("Arrow_socket"));
		return;
	}
	AcquireServerPoseRefresh();

	bIsDrawing = true;
	bIsFullyDrawn = false;
	bIsReleaseInProgress = false;
	bHasFiredCurrentShot = false;
	bHasReceivedNockNotify = false;
	DrawStartTime = GetWorld()->GetTimeSeconds();
	CachedBowComponent->SetDrawAlpha(0.0f);
	CachedBowComponent->SetArrowNocked(false);

	SetAttackTag(true);
	if (!IsAimSessionActive()) return;
	SetBowDrawTagState(true, false, false);

	if (ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo()))
	{
		Player->StopSprint();
	}

	if (IsUsingAimCycleMontage())
	{
		PlayAimCycleMontage();
	}
	else
	{
		PlayDrawMontage();
	}

	if (!IsAimSessionActive() || bFinishingShot) return;
	GetWorld()->GetTimerManager().SetTimer(
		ChargeTimerHandle,
		this,
		&UGA_BowAimFire::UpdateDrawAlpha,
		ChargeTickRate,
		true);
}

void UGA_BowAimFire::OnLeftClickReleased(FGameplayEventData Payload)
{
	if (!IsAimSessionActive() || !CachedBowComponent || bIsReleaseInProgress)
	{
		return;
	}

	if (bIsFullyDrawn)
	{
		BeginRelease(Payload);
		return;
	}

	if (bIsDrawing)
	{
		FinishShot();
	}
}

void UGA_BowAimFire::OnRightClickReleased(float TimeHeld)
{
	if (IsAimSessionActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	}
}

void UGA_BowAimFire::UpdateDrawAlpha()
{
	if (!IsAimSessionActive() || bFinishingShot || !CachedBowComponent || !bIsDrawing)
	{
		return;
	}

	float DrawAlpha = 0.f;
	if (bUseAimCycleDrawAlphaCurve && IsUsingAimCycleMontage())
	{
		if (const FWeaponAnimationEntry* Entry = GetBowAnimationEntry())
		{
			if (const UAnimInstance* AnimInstance = CurrentActorInfo ? CurrentActorInfo->GetAnimInstance() : nullptr)
			{
				DrawAlpha = FMath::Clamp(AnimInstance->GetCurveValue(Entry->DrawAlphaCurveName), 0.f, 1.f);
			}
		}
	}
	else
	{
		const float HeldTime = GetWorld()->GetTimeSeconds() - DrawStartTime;
		const float DrawDuration = FMath::Max(FullDrawTime - DrawAlphaStartDelay, KINDA_SMALL_NUMBER);
		DrawAlpha = FMath::Clamp((HeldTime - DrawAlphaStartDelay) / DrawDuration, 0.0f, 1.0f);
	}
	CachedBowComponent->SetDrawAlpha(DrawAlpha);

	if (DrawAlpha >= FullDrawAlphaToRelease)
	{
		GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);
		CachedBowComponent->SetDrawAlpha(1.0f);

		// Make the full-draw pose available before the draw montage starts blending
		// out. This prevents the underlying unaimed bow overlay from flashing.
		SetBowDrawTagState(false, true, false);
		if (IsUsingAimCycleMontage())
		{
			JumpAimCycleToSection(GetBowAnimationEntry()->AimCycleHoldSectionName);
		}
		else
		{
			StopDrawMontage(DrawMontageBlendOutTime);
		}
	}
}

void UGA_BowAimFire::PlayAimCycleMontage()
{
	UAnimMontage* AimCycleMontage = GetAimCycleMontage();
	const FWeaponAnimationEntry* Entry = GetBowAnimationEntry();
	if (!AimCycleMontage || !Entry)
	{
		return;
	}

	BowMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this,
		NAME_None,
		AimCycleMontage,
		Entry->AimCyclePlayRate,
		Entry->AimCycleDrawSectionName,
		true, 1.f, 0.f, true);

	if (BowMontageTask)
	{
		BowMontageTask->OnCompleted.AddDynamic(this, &UGA_BowAimFire::OnAimCycleMontageCompleted);
		BowMontageTask->OnInterrupted.AddDynamic(this, &UGA_BowAimFire::OnAimCycleMontageInterrupted);
		BowMontageTask->OnCancelled.AddDynamic(this, &UGA_BowAimFire::OnAimCycleMontageInterrupted);
		BowMontageTask->ReadyForActivation();
	}
}

void UGA_BowAimFire::StopAimCycleMontage(float BlendOutTime)
{
	StopBowMontage(BlendOutTime);
}

void UGA_BowAimFire::JumpAimCycleToSection(FName SectionName)
{
	if (UAnimMontage* AimCycleMontage = GetAimCycleMontage())
	{
		if (UAnimInstance* AnimInstance = CurrentActorInfo ? CurrentActorInfo->GetAnimInstance() : nullptr)
		{
			AnimInstance->Montage_JumpToSection(SectionName, AimCycleMontage);
		}
	}
}

void UGA_BowAimFire::PlayDrawMontage()
{
	if (!DrawMontage)
	{
		return;
	}

	BowMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this,
		NAME_None,
		DrawMontage,
		DrawMontagePlayRate,
		NAME_None,
		true, 1.f, 0.f, true);

	if (BowMontageTask)
	{
		BowMontageTask->OnInterrupted.AddDynamic(this, &UGA_BowAimFire::OnAimCycleMontageInterrupted);
		BowMontageTask->OnCancelled.AddDynamic(this, &UGA_BowAimFire::OnAimCycleMontageInterrupted);
		BowMontageTask->ReadyForActivation();
	}
}

void UGA_BowAimFire::StopDrawMontage(float BlendOutTime)
{
	StopBowMontage(BlendOutTime);
}

void UGA_BowAimFire::BeginRelease(const FGameplayEventData& ReleaseInput)
{
	if (!IsAimSessionActive() || !CachedBowComponent || bIsReleaseInProgress || bHasFiredCurrentShot)
	{
		return;
	}

	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	UPlayerAimComponent* Aim = Player ? Player->GetAimComponent() : nullptr;
	if (!Aim || !Aim->BeginShot(ReleaseInput, PendingShotId))
	{
		UE_LOG(LogTemp, Warning, TEXT("Bow release rejected: missing or duplicate shot ID."));
		FinishShot();
		return;
	}

	if (GetAvatarActorFromActorInfo() && GetAvatarActorFromActorInfo()->HasAuthority())
	{
		const float HeldTime = GetWorld()->GetTimeSeconds() - DrawStartTime;
		ServerReleaseDrawAlpha = FMath::Clamp((HeldTime - DrawAlphaStartDelay)
			/ FMath::Max(FullDrawTime - DrawAlphaStartDelay, KINDA_SMALL_NUMBER), 0.f, 1.f);
		PendingReleaseFireSpeed = CachedBowComponent->GetFireSpeed(ServerReleaseDrawAlpha);
		if (PendingReleaseFireSpeed <= 0.f)
		{
			FinishShot();
			return;
		}
	}
	else
	{
		PendingReleaseFireSpeed = CachedBowComponent->GetFireSpeed(CachedBowComponent->GetDrawAlpha());
	}

	GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);
	bIsDrawing = false;
	bIsFullyDrawn = true;
	bIsReleaseInProgress = true;
	bReleaseMontageFinished = false;
	SetBowDrawTagState(false, true, true);

	if (!bRequireReleaseNotifyToFire)
	{
		CachedBowComponent->SetDrawAlpha(0.0f);
	}

	if (IsUsingAimCycleMontage())
	{
		JumpAimCycleToSection(GetBowAnimationEntry()->AimCycleReleaseSectionName);
		if (!bRequireReleaseNotifyToFire)
		{
			QueueReleaseShot();
		}
		return;
	}

	StopDrawMontage(0.0f);

	if (ReleaseMontage)
	{
		BowMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			NAME_None,
			ReleaseMontage,
			ReleaseMontagePlayRate,
			NAME_None,
			true, 1.f, 0.f, true);

		if (BowMontageTask)
		{
			BowMontageTask->OnCompleted.AddDynamic(this, &UGA_BowAimFire::OnReleaseMontageCompleted);
			BowMontageTask->OnInterrupted.AddDynamic(this, &UGA_BowAimFire::OnReleaseMontageInterrupted);
			BowMontageTask->OnCancelled.AddDynamic(this, &UGA_BowAimFire::OnReleaseMontageInterrupted);
			BowMontageTask->ReadyForActivation();
			if (!IsAimSessionActive()) return;

			if (!bRequireReleaseNotifyToFire)
			{
				QueueReleaseShot();
			}
			return;
		}
	}

	bReleaseMontageFinished = true;
	QueueReleaseShot();
}

void UGA_BowAimFire::OnReleaseFireEvent(FGameplayEventData /*TimingPayload*/)
{
	if (!IsAimSessionActive() || !bIsReleaseInProgress || !bIsFullyDrawn || bHasFiredCurrentShot)
	{
		return;
	}

	if (CachedBowComponent)
	{
		CachedBowComponent->SetDrawAlpha(0.0f);
		CachedBowComponent->SetArrowNocked(false);
	}

	// Late commit reads the camera and socket after physics/based movement have completed.
	QueueReleaseShot();
}

void UGA_BowAimFire::OnNockArrowEvent(FGameplayEventData Payload)
{
	if (!IsAimSessionActive() || !CachedBowComponent || bIsReleaseInProgress
		|| (!bIsDrawing && !bIsFullyDrawn))
	{
		return;
	}

	bHasReceivedNockNotify = true;
	CachedBowComponent->SetArrowNocked(true);
}

void UGA_BowAimFire::OnReleaseMontageCompleted()
{
	if (!IsAimSessionActive())
	{
		return;
	}

	bReleaseMontageFinished = true;
	if (!bReleaseQueued) FinishShot();
}

void UGA_BowAimFire::OnReleaseMontageInterrupted()
{
	if (IsAimSessionActive()) EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UGA_BowAimFire::OnAimCycleMontageCompleted()
{
	if (IsAimSessionActive())
	{
		OnReleaseMontageCompleted();
	}
}

void UGA_BowAimFire::OnAimCycleMontageInterrupted()
{
	if (IsAimSessionActive()) EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

bool UGA_BowAimFire::IsAimPathObstructed() const
{
	const ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!IsAimSessionActive() || !Player || !IsValid(CachedBow) || !CachedBowComponent
		|| !Player->GetAimComponent() || Player->EquippedItem != CachedBow) return false;
	const UClass* SpawnClass = CachedBow->GetSpawnClass();
	if (!SpawnClass || !SpawnClass->IsChildOf(APlayerArrowProjectile::StaticClass())) return false;
	const AArrowProjectile* Defaults = SpawnClass->GetDefaultObject<AArrowProjectile>();
	FProjectileShotInput Input;
	Input.ShotId = FGuid(0, 0, 0, 1); // Local preview only; never queued, sent or spawned.
	Input.Speed = Defaults->ResolveInitialLaunchSpeed(CachedBowComponent->GetFireSpeed(CachedBowComponent->GetDrawAlpha()));
	Input.GravityZ = GetWorld()->GetGravityZ() * PlayerBowShotPreparation::GetGravityScale(Defaults->GetFlightGravityScale());
	if (!CachedBow->TryGetArrowSpawnTransform(Input.MuzzleTransform)
		|| !Player->GetAimComponent()->ResolveCurrentAim(CachedBow, Input.MuzzleTransform.GetLocation(), Input.AimPoint, Input.AimDirection, Input.AimServerTime)) return false;
	FProjectileShotSnapshot Shot;
	if (!PlayerBowShotPreparation::Prepare(Player, Input, Shot)) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BowAimClearance), false, Player);
	Params.bFindInitialOverlaps = true;
	Params.AddIgnoredActor(CachedBow);
	FHitResult Hit;
	return ArrowCollisionQuery::IsAimObstructed(GetWorld(), Shot, Defaults->GetObstacleCollisionHalfExtent(), Params, Hit);
}

void UGA_BowAimFire::QueueReleaseShot()
{
	if (!IsAimSessionActive() || !bIsReleaseInProgress || bHasFiredCurrentShot || bReleaseQueued || !PendingShotId.IsValid()) return;
	ShotComponent = UProjectileShotComponent::FindOrAdd(GetAvatarActorFromActorInfo());
	if (!ShotComponent.IsValid()) { FinishShot(); return; }
	bReleaseQueued = ShotComponent->Queue(this, PendingShotId,
		FProjectileShotCommitDelegate::CreateUObject(this, &UGA_BowAimFire::CommitReleaseShot, PendingShotId),
		FProjectileShotFinishedDelegate::CreateUObject(this, &UGA_BowAimFire::OnShotCommitted, PendingShotId),
		GetAvatarActorFromActorInfo()->HasAuthority() ? 1.0f : 2.0f);
	if (!bReleaseQueued) FinishShot();
}

EProjectileShotCommit UGA_BowAimFire::CommitReleaseShot(FGuid ShotId)
{
	if (!ShotId.IsValid() || ShotId != PendingShotId) return EProjectileShotCommit::Rejected;
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!IsAimSessionActive() || bHasFiredCurrentShot || !bIsReleaseInProgress || !bIsFullyDrawn
		|| !Player || !IsValid(CachedBow) || !CachedBowComponent
		|| Player->EquippedItem != CachedBow || GetSourceWeapon() != CachedBow) return EProjectileShotCommit::Rejected;
	if (!bHasReceivedNockNotify)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ProjectileShot] Missing NockArrow notify Id=%s"), *PendingShotId.ToString());
		return EProjectileShotCommit::Rejected;
	}

	UPlayerAimComponent* Aim = Player->GetAimComponent();
	if (!Aim) return EProjectileShotCommit::Rejected;
	if (Player->IsLocallyControlled() && !Aim->CaptureShotView(PendingShotId))
		return EProjectileShotCommit::Rejected;
	if (!Player->HasAuthority() && bLocalShotPresented)
	{
		bool bSucceeded = false;
		if (!Aim->TryGetShotResolution(PendingShotId, bSucceeded)) return EProjectileShotCommit::Pending;
		bHasFiredCurrentShot = bSucceeded;
		return bSucceeded ? EProjectileShotCommit::Succeeded : EProjectileShotCommit::Rejected;
	}

	FProjectileShotInput Input;
	Input.ShotId = PendingShotId;
	UClass* SpawnClass = CachedBow->GetSpawnClass();
	if (!SpawnClass || !SpawnClass->IsChildOf(APlayerArrowProjectile::StaticClass())
		|| !CachedBow->TryGetArrowSpawnTransform(Input.MuzzleTransform)) return EProjectileShotCommit::Rejected;
	const EPlayerShotAimResult AimResult = Aim->ResolveShotAim(PendingShotId, CachedBow, Input.MuzzleTransform.GetLocation(),
		Input.AimPoint, Input.AimDirection, Input.AimServerTime);
	if (AimResult == EPlayerShotAimResult::Pending) return EProjectileShotCommit::Pending;
	if (AimResult != EPlayerShotAimResult::Ready) return EProjectileShotCommit::Rejected;

	const AArrowProjectile* Defaults = SpawnClass->GetDefaultObject<AArrowProjectile>();
	Input.Speed = Defaults->ResolveInitialLaunchSpeed(PendingReleaseFireSpeed);
	Input.GravityZ = GetWorld()->GetGravityZ() * PlayerBowShotPreparation::GetGravityScale(Defaults->GetFlightGravityScale());
	FProjectileShotSnapshot Shot;
	if (!PlayerBowShotPreparation::Prepare(Player, Input, Shot))
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlayerBowShot] Invalid launch input or unavailable supported ship motion Id=%s"), *PendingShotId.ToString());
		return EProjectileShotCommit::Rejected;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(BowReleaseClearance), false, Player);
	Params.bFindInitialOverlaps = true;
	Params.AddIgnoredActor(CachedBow);
	FHitResult Obstacle;
	const bool bObstructed = ArrowCollisionQuery::IsAimObstructed(GetWorld(), Shot,
		Defaults->GetObstacleCollisionHalfExtent(), Params, Obstacle);
	Aim->ReportShotObstruction(bObstructed);
	// Feedback does not deal damage or skip world collisions. The first flight sweep owns impact.
	CachedBowComponent->SetArrowNocked(false);
	if (!Player->HasAuthority())
	{
		bLocalShotPresented = true;
		return EProjectileShotCommit::Pending;
	}

	APlayerArrowProjectile* Arrow = GetWorld()->SpawnActorDeferred<APlayerArrowProjectile>(
		SpawnClass, Shot.SpawnTransform, Player, Player, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Arrow) return EProjectileShotCommit::Rejected;
	Arrow->IgnoreActorForMovement(Player);
	Arrow->IgnoreActorForMovement(CachedBow);
	Arrow->FinishSpawning(Shot.SpawnTransform);

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const auto* Definition = CachedBow->GetWeaponDefinition();
	if (!ASC || !Definition || !Definition->CombatData
		|| !FMath::IsFinite(MinChargeDamageMultiplier) || !FMath::IsFinite(MaxChargeDamageMultiplier)
		|| MinChargeDamageMultiplier <= 0.f || MaxChargeDamageMultiplier < MinChargeDamageMultiplier)
	{
		Arrow->Destroy();
		return EProjectileShotCommit::Rejected;
	}
	FStrengthDamageRequest DamageRequest;
	DamageRequest.SourceASC = ASC;
	DamageRequest.AttackCoefficient = Definition->CombatData->AttackCoefficient;
	DamageRequest.ChargeMultiplier = FMath::Lerp(MinChargeDamageMultiplier, MaxChargeDamageMultiplier, ServerReleaseDrawAlpha);
	DamageRequest.InstigatorActor = Player;
	DamageRequest.EffectCauser = Arrow;
	DamageRequest.EffectLevel = Arrow->GetDirectDamageEffectLevel();
	const FGameplayEffectSpecHandle DamageSpec = UGASCombatLibrary::MakeStrengthDamageEffectSpec(DamageRequest);
	if (!DamageSpec.IsValid() || !Arrow->InitializeStrengthDamage(ASC, Player, DamageSpec))
	{
		Arrow->Destroy();
		return EProjectileShotCommit::Rejected;
	}
	if (!Arrow->LaunchPlayerShot(Shot, CachedBow)) { Arrow->Destroy(); return EProjectileShotCommit::Rejected; }
	CachedBow->Multicast_PlayReleaseFX();
	bHasFiredCurrentShot = true;
	CachedBowComponent->SetDrawAlpha(0.0f);
	return EProjectileShotCommit::Succeeded;
}

void UGA_BowAimFire::OnShotCommitted(bool bSucceeded, FGuid ShotId)
{
	if (!IsAimSessionActive() || bFinishingShot || !ShotId.IsValid() || ShotId != PendingShotId) return;
	bReleaseQueued = false;
	if (ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo()))
	{
		if (UPlayerAimComponent* Aim = Player->GetAimComponent())
		{
			Aim->CompleteShot(PendingShotId, bSucceeded);
			Aim->EndShot(PendingShotId);
		}
	}
	if (!bSucceeded || bReleaseMontageFinished) FinishShot();
}

void UGA_BowAimFire::CancelPendingShot()
{
	if (ShotComponent.IsValid()) ShotComponent->Cancel(PendingShotId);
	if (ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo()))
	{
		if (UPlayerAimComponent* Aim = Player->GetAimComponent())
		{
			Aim->CompleteShot(PendingShotId, false);
			Aim->EndShot(PendingShotId);
		}
	}
	PendingShotId.Invalidate();
	bReleaseQueued = false;
	bLocalShotPresented = false;
	bReleaseMontageFinished = false;
}
void UGA_BowAimFire::FinishShot()
{
	if (bFinishingShot) return;
	TGuardValue<bool> FinishingGuard(bFinishingShot, true);
	CancelPendingShot();
	GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);
	const FWeaponAnimationEntry* Entry = GetBowAnimationEntry();
	StopBowMontage(Entry ? Entry->AimCycleBlendOutTime : DrawMontageBlendOutTime);
	RemoveBowStateTags();

	bIsDrawing = false;
	bIsFullyDrawn = false;
	bIsReleaseInProgress = false;
	bHasFiredCurrentShot = false;
	bHasReceivedNockNotify = false;
	PendingReleaseFireSpeed = 0.f;
	PendingShotId.Invalidate();
	ServerReleaseDrawAlpha = 0.f;

	if (CachedBowComponent)
	{
		CachedBowComponent->SetDrawAlpha(0.0f);
		CachedBowComponent->SetArrowNocked(false);
	}

	SetAttackTag(false);
	ReleaseServerPoseRefresh();
}

void UGA_BowAimFire::ResetBowState()
{
	FinishShot();
	if (CachedBowComponent) CachedBowComponent->SetAiming(false);
}

void UGA_BowAimFire::AcquireServerPoseRefresh()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!bOwnsServerPoseRefresh && Player && Player->HasAuthority())
	{
		Player->AcquireServerCombatPoseRefresh();
		bOwnsServerPoseRefresh = true;
	}
}

void UGA_BowAimFire::ReleaseServerPoseRefresh()
{
	if (!bOwnsServerPoseRefresh)
	{
		return;
	}

	if (ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo()))
	{
		Player->ReleaseServerCombatPoseRefresh();
	}
	bOwnsServerPoseRefresh = false;
}

bool UGA_BowAimFire::CacheBowFromAvatar()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player || !IsValid(Player->EquippedItem))
	{
		CachedBow = nullptr;
		CachedBowComponent = nullptr;
		return false;
	}

	CachedBow = Cast<ABowItem>(GetSourceWeapon());
	CachedBowComponent = CachedBow ? CachedBow->GetBowComponent() : nullptr;

	return CachedBow && CachedBowComponent;
}

const FWeaponAnimationEntry* UGA_BowAimFire::GetBowAnimationEntry() const
{
	const ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (const UPlayerEquipmentComponent* EquipmentComponent = Player ? Player->GetEquipmentComponent() : nullptr)
	{
		return EquipmentComponent->GetEquippedWeaponAnimationEntry();
	}

	return nullptr;
}

UAnimMontage* UGA_BowAimFire::GetAimCycleMontage() const
{
	const FWeaponAnimationEntry* Entry = GetBowAnimationEntry();
	return Entry ? Entry->AimCycleMontage.Get() : nullptr;
}

bool UGA_BowAimFire::IsUsingAimCycleMontage() const
{
	return GetAimCycleMontage() != nullptr;
}

void UGA_BowAimFire::AddBowStateTags()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		auto AddOwnedTag = [this, ASC](FGameplayTag Tag, bool bEnabled)
		{
			if (bEnabled && !OwnedBowStateTags.HasTagExact(Tag))
			{
				OwnedBowStateTags.AddTag(Tag);
				ASC->AddLooseGameplayTag(Tag);
			}
		};
		AddOwnedTag(State_Bow_Drawing, bIsDrawing);
		AddOwnedTag(State_Bow_FullyDrawn, bIsFullyDrawn);
		AddOwnedTag(State_Bow_Releasing, bIsReleaseInProgress);
	}
}

void UGA_BowAimFire::RemoveBowStateTags()
{
	const FGameplayTagContainer TagsToRemove = OwnedBowStateTags;
	OwnedBowStateTags.Reset();
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		ASC->RemoveLooseGameplayTags(TagsToRemove);
}

void UGA_BowAimFire::SetBowDrawTagState(bool bDrawing, bool bFullyDrawn, bool bReleasing)
{
	RemoveBowStateTags();

	bIsDrawing = bDrawing;
	bIsFullyDrawn = bFullyDrawn;
	bIsReleaseInProgress = bReleasing;

	AddBowStateTags();
}

bool UGA_BowAimFire::IsAimSessionActive() const
{
	return IsActive() && !bEndingAim;
}

void UGA_BowAimFire::SetAttackTag(bool bEnabled)
{
	if (bOwnsAttackTag == bEnabled) return;
	bOwnsAttackTag = bEnabled;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		if (bEnabled) ASC->AddLooseGameplayTag(State_Attacking);
		else ASC->RemoveLooseGameplayTag(State_Attacking);
	}
}

void UGA_BowAimFire::StopBowMontage(float BlendOutTime)
{
	if (!BowMontageTask) return;
	UAbilityTask_PlayMontageAndWait* Task = BowMontageTask;
	BowMontageTask = nullptr;
	Task->OnCompleted.RemoveAll(this);
	Task->OnInterrupted.RemoveAll(this);
	Task->OnCancelled.RemoveAll(this);
	// Stop only our montage; a hit-reaction montage may already own the ASC.
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		if (ASC->GetAnimatingAbility() == this) ASC->CurrentMontageStop(FMath::Max(0.f, BlendOutTime));
	Task->EndTask();
}
