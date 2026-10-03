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
	SetAssetTags(AssetTags);
}

void UGA_BowAimFire::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CacheBowFromAvatar())
	{
		UE_LOG(LogTemp, Warning, TEXT("UGA_BowAimFire::ActivateAbility : Equipped item is not a valid bow."));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(State_Aiming);
	}

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
		WaitRightReleaseTask->ReadyForActivation();
	}

	UAbilityTask_WaitGameplayEvent* WaitLeftPressedTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Key_Default_Mouse_LeftClick, nullptr, false, true);
	if (WaitLeftPressedTask)
	{
		WaitLeftPressedTask->EventReceived.AddDynamic(this, &UGA_BowAimFire::OnLeftClickPressed);
		WaitLeftPressedTask->ReadyForActivation();
	}

	UAbilityTask_WaitGameplayEvent* WaitLeftReleasedTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Key_Default_Mouse_LeftClick_Released, nullptr, false, true);
	if (WaitLeftReleasedTask)
	{
		WaitLeftReleasedTask->EventReceived.AddDynamic(this, &UGA_BowAimFire::OnLeftClickReleased);
		WaitLeftReleasedTask->ReadyForActivation();
	}

	UAbilityTask_WaitGameplayEvent* WaitFireArrowTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Event_Montage_FireArrow, nullptr, false, true);
	if (WaitFireArrowTask)
	{
		WaitFireArrowTask->EventReceived.AddDynamic(this, &UGA_BowAimFire::OnReleaseFireEvent);
		WaitFireArrowTask->ReadyForActivation();
	}

	UAbilityTask_WaitGameplayEvent* WaitNockArrowTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Event_Montage_NockArrow, nullptr, false, true);
	if (WaitNockArrowTask)
	{
		WaitNockArrowTask->EventReceived.AddDynamic(this, &UGA_BowAimFire::OnNockArrowEvent);
		WaitNockArrowTask->ReadyForActivation();
	}
}

void UGA_BowAimFire::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo()))
		if (Player->GetAimComponent()) Player->GetAimComponent()->SetObstructionQuery(FPlayerAimObstructionQuery());
	ResetBowState();

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(State_Aiming);
		ASC->RemoveLooseGameplayTag(State_Attacking);
	}
	RemoveBowStateTags();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_BowAimFire::OnLeftClickPressed(FGameplayEventData Payload)
{
	if (!IsActive() || !CachedBowComponent || bIsDrawing || bIsFullyDrawn)
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

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(State_Attacking);
	}
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

	GetWorld()->GetTimerManager().SetTimer(
		ChargeTimerHandle,
		this,
		&UGA_BowAimFire::UpdateDrawAlpha,
		ChargeTickRate,
		true);
}

void UGA_BowAimFire::OnLeftClickReleased(FGameplayEventData Payload)
{
	if (!IsActive() || !CachedBowComponent || bIsReleaseInProgress)
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
	if (IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	}
}

void UGA_BowAimFire::UpdateDrawAlpha()
{
	if (!CachedBowComponent)
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

	UAbilityTask_PlayMontageAndWait* AimCycleTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this,
		NAME_None,
		AimCycleMontage,
		Entry->AimCyclePlayRate,
		Entry->AimCycleDrawSectionName,
		true);

	if (AimCycleTask)
	{
		AimCycleTask->OnCompleted.AddDynamic(this, &UGA_BowAimFire::OnAimCycleMontageCompleted);
		AimCycleTask->OnInterrupted.AddDynamic(this, &UGA_BowAimFire::OnAimCycleMontageInterrupted);
		AimCycleTask->OnCancelled.AddDynamic(this, &UGA_BowAimFire::OnAimCycleMontageInterrupted);
		AimCycleTask->ReadyForActivation();
	}
}

void UGA_BowAimFire::StopAimCycleMontage(float BlendOutTime)
{
	UAnimMontage* AimCycleMontage = GetAimCycleMontage();
	if (!AimCycleMontage || !CurrentActorInfo)
	{
		return;
	}

	if (UAnimInstance* AnimInstance = CurrentActorInfo->GetAnimInstance())
	{
		if (AnimInstance->Montage_IsPlaying(AimCycleMontage))
		{
			AnimInstance->Montage_Stop(FMath::Max(0.f, BlendOutTime), AimCycleMontage);
		}
	}
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

	UAbilityTask_PlayMontageAndWait* DrawMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this,
		NAME_None,
		DrawMontage,
		DrawMontagePlayRate,
		NAME_None,
		true);

	if (DrawMontageTask)
	{
		DrawMontageTask->ReadyForActivation();
	}
}

void UGA_BowAimFire::StopDrawMontage(float BlendOutTime)
{
	if (!DrawMontage || !CurrentActorInfo)
	{
		return;
	}

	if (UAnimInstance* AnimInstance = CurrentActorInfo->GetAnimInstance())
	{
		if (AnimInstance->Montage_IsPlaying(DrawMontage))
		{
			AnimInstance->Montage_Stop(FMath::Max(0.f, BlendOutTime), DrawMontage);
		}
	}
}

void UGA_BowAimFire::BeginRelease(const FGameplayEventData& ReleaseInput)
{
	if (!IsActive() || !CachedBowComponent || bIsReleaseInProgress || bHasFiredCurrentShot)
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
		UAbilityTask_PlayMontageAndWait* ReleaseMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			NAME_None,
			ReleaseMontage,
			ReleaseMontagePlayRate,
			NAME_None,
			true, 1.f, 0.f, true);

		if (ReleaseMontageTask)
		{
			ReleaseMontageTask->OnCompleted.AddDynamic(this, &UGA_BowAimFire::OnReleaseMontageCompleted);
			ReleaseMontageTask->OnInterrupted.AddDynamic(this, &UGA_BowAimFire::OnReleaseMontageInterrupted);
			ReleaseMontageTask->OnCancelled.AddDynamic(this, &UGA_BowAimFire::OnReleaseMontageInterrupted);
			ReleaseMontageTask->ReadyForActivation();

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
	if (!IsActive() || !bIsReleaseInProgress || !bIsFullyDrawn || bHasFiredCurrentShot)
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
	if (!IsActive() || !CachedBowComponent || bIsReleaseInProgress
		|| (!bIsDrawing && !bIsFullyDrawn))
	{
		return;
	}

	bHasReceivedNockNotify = true;
	CachedBowComponent->SetArrowNocked(true);
}

void UGA_BowAimFire::OnReleaseMontageCompleted()
{
	if (!IsActive())
	{
		return;
	}

	bReleaseMontageFinished = true;
	if (!bReleaseQueued) FinishShot();
}

void UGA_BowAimFire::OnReleaseMontageInterrupted()
{
	if (!IsActive())
	{
		return;
	}

	FinishShot();
}

void UGA_BowAimFire::OnAimCycleMontageCompleted()
{
	if (IsActive())
	{
		OnReleaseMontageCompleted();
	}
}

void UGA_BowAimFire::OnAimCycleMontageInterrupted()
{
	if (IsActive())
	{
		FinishShot();
	}
}

bool UGA_BowAimFire::IsAimPathObstructed() const
{
	const ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!IsActive() || !Player || !IsValid(CachedBow) || !CachedBowComponent
		|| !Player->GetAimComponent() || Player->EquippedItem != CachedBow) return false;
	const UClass* SpawnClass = CachedBow->GetSpawnClass();
	if (!SpawnClass || !SpawnClass->IsChildOf(APlayerArrowProjectile::StaticClass())) return false;
	const AArrowProjectile* Defaults = SpawnClass->GetDefaultObject<AArrowProjectile>();
	FProjectileShotInput Input;
	Input.ShotId = FGuid(0, 0, 0, 1); // Local preview only; never queued, sent or spawned.
	Input.Speed = CachedBowComponent->GetFireSpeed(CachedBowComponent->GetDrawAlpha());
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
	if (!IsActive() || !bIsReleaseInProgress || bHasFiredCurrentShot || bReleaseQueued || !PendingShotId.IsValid()) return;
	ShotComponent = UProjectileShotComponent::FindOrAdd(GetAvatarActorFromActorInfo());
	if (!ShotComponent.IsValid()) { FinishShot(); return; }
	bReleaseQueued = ShotComponent->Queue(this, PendingShotId,
		FProjectileShotCommitDelegate::CreateUObject(this, &UGA_BowAimFire::CommitReleaseShot),
		FProjectileShotFinishedDelegate::CreateUObject(this, &UGA_BowAimFire::OnShotCommitted),
		GetAvatarActorFromActorInfo()->HasAuthority() ? 1.0f : 2.0f);
	if (!bReleaseQueued) FinishShot();
}

EProjectileShotCommit UGA_BowAimFire::CommitReleaseShot()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!IsActive() || bHasFiredCurrentShot || !bIsReleaseInProgress || !bIsFullyDrawn
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
	Input.Speed = PendingReleaseFireSpeed;
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

void UGA_BowAimFire::OnShotCommitted(bool bSucceeded)
{
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
	StopDrawMontage(DrawMontageBlendOutTime);
	if (const FWeaponAnimationEntry* Entry = GetBowAnimationEntry())
	{
		StopAimCycleMontage(Entry->AimCycleBlendOutTime);
	}

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

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(State_Attacking);
	}
	RemoveBowStateTags();
	ReleaseServerPoseRefresh();
}

void UGA_BowAimFire::ResetBowState()
{
	if (bFinishingShot) return;
	TGuardValue<bool> FinishingGuard(bFinishingShot, true);
	CancelPendingShot();
	GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);
	StopDrawMontage(DrawMontageBlendOutTime);
	if (const FWeaponAnimationEntry* Entry = GetBowAnimationEntry())
	{
		StopAimCycleMontage(Entry->AimCycleBlendOutTime);
	}
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
		CachedBowComponent->SetAiming(false);
		CachedBowComponent->SetArrowNocked(false);
	}
	RemoveBowStateTags();
	ReleaseServerPoseRefresh();
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
		if (bIsDrawing)
		{
			ASC->AddLooseGameplayTag(State_Bow_Drawing);
		}
		if (bIsFullyDrawn)
		{
			ASC->AddLooseGameplayTag(State_Bow_FullyDrawn);
		}
		if (bIsReleaseInProgress)
		{
			ASC->AddLooseGameplayTag(State_Bow_Releasing);
		}
	}
}

void UGA_BowAimFire::RemoveBowStateTags()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(State_Bow_Drawing);
		ASC->RemoveLooseGameplayTag(State_Bow_FullyDrawn);
		ASC->RemoveLooseGameplayTag(State_Bow_Releasing);
	}
}

void UGA_BowAimFire::SetBowDrawTagState(bool bDrawing, bool bFullyDrawn, bool bReleasing)
{
	RemoveBowStateTags();

	bIsDrawing = bDrawing;
	bIsFullyDrawn = bFullyDrawn;
	bIsReleaseInProgress = bReleasing;

	AddBowStateTags();
}
