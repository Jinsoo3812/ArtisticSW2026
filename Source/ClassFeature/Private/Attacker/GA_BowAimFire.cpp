#include "Attacker/GA_BowAimFire.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "Animation/AnimInstance.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Equipment/PlayerEquipmentComponent.h"
#include "Equipment/WeaponAnimationDataAsset.h"
#include "Item/Components/BowComponent.h"
#include "Item/Projectiles/ArrowProjectile.h"
#include "Item/Weapons/BowItem.h"
#include "GASCombatLibrary.h"
#include "GASAttributeDamageGameplayEffect.h"

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
		&& !CachedBowComponent->TryBuildArrowSpawnTransform(ArrowSpawnTransform))
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

void UGA_BowAimFire::BeginRelease(const FGameplayEventData& Payload)
{
	if (!IsActive() || !CachedBowComponent || bIsReleaseInProgress || bHasFiredCurrentShot)
	{
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);
	bIsDrawing = false;
	bIsFullyDrawn = true;
	bIsReleaseInProgress = true;
	ReleasePayload = Payload;
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
			FireArrow(ReleasePayload);
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
			true);

		if (ReleaseMontageTask)
		{
			ReleaseMontageTask->OnCompleted.AddDynamic(this, &UGA_BowAimFire::OnReleaseMontageCompleted);
			ReleaseMontageTask->OnBlendOut.AddDynamic(this, &UGA_BowAimFire::OnReleaseMontageCompleted);
			ReleaseMontageTask->OnInterrupted.AddDynamic(this, &UGA_BowAimFire::OnReleaseMontageInterrupted);
			ReleaseMontageTask->OnCancelled.AddDynamic(this, &UGA_BowAimFire::OnReleaseMontageInterrupted);
			ReleaseMontageTask->ReadyForActivation();

			if (!bRequireReleaseNotifyToFire)
			{
				FireArrow(ReleasePayload);
			}
			return;
		}
	}

	FireArrow(ReleasePayload);
	FinishShot();
}

void UGA_BowAimFire::OnReleaseFireEvent(FGameplayEventData Payload)
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

	FireArrow(ReleasePayload);
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

	FinishShot();
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
		FinishShot();
	}
}

void UGA_BowAimFire::OnAimCycleMontageInterrupted()
{
	if (IsActive())
	{
		FinishShot();
	}
}

void UGA_BowAimFire::FireArrow(const FGameplayEventData& Payload)
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (bHasFiredCurrentShot || !bIsFullyDrawn
		|| !Player || !CachedBow || !CachedBowComponent)
	{
		return;
	}
	if (!bHasReceivedNockNotify)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("UGA_BowAimFire::FireArrow: Event.Montage.NockArrow was not received; shot rejected."));
		return;
	}

	// Presentation is predicted on the owning client and repeated authoritatively on the server.
	// Only the server continues into projectile creation.
	CachedBowComponent->SetArrowNocked(false);
	if (!Player->HasAuthority())
	{
		return;
	}

	UClass* SpawnClass = CachedBow->GetSpawnClass();
	if (!SpawnClass || !SpawnClass->IsChildOf(AArrowProjectile::StaticClass()))
	{
		UE_LOG(LogTemp, Warning, TEXT("UGA_BowAimFire::FireArrow : Bow SpawnClass must derive from AArrowProjectile."));
		return;
	}

	FTransform SpawnTransform;
	if (!CachedBowComponent->TryBuildArrowSpawnTransform(SpawnTransform))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("UGA_BowAimFire::FireArrow: Bow %s is missing required socket %s. Arrow will not fire."),
			*GetNameSafe(CachedBow),
			*CachedBow->GetCharacterArrowSocketName().ToString());
		return;
	}
	const FVector SpawnLocation = SpawnTransform.GetLocation();

	FVector AimTarget;
	if (!TryGetAimTargetFromPayload(Payload, AimTarget))
	{
		UE_LOG(LogTemp, Warning, TEXT("UGA_BowAimFire::FireArrow : Missing client aim target payload. Arrow will not fire."));
		return;
	}

	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(Player);
	ActorsToIgnore.Add(CachedBow);

	FVector ServerAimTarget;
	if (!CachedBowComponent->ResolveAimTargetFromSocket(SpawnLocation, AimTarget, ActorsToIgnore, ServerAimTarget))
	{
		UE_LOG(LogTemp, Warning, TEXT("UGA_BowAimFire::FireArrow : Could not resolve server aim target from arrow socket. Arrow will not fire."));
		return;
	}

	FVector LaunchVelocity;
	if (!CachedBowComponent->TryCalculateLaunchVelocity(SpawnLocation, ServerAimTarget, ActorsToIgnore, LaunchVelocity))
	{
		UE_LOG(LogTemp, Warning, TEXT("UGA_BowAimFire::FireArrow : Could not resolve launch velocity to client aim target. Arrow will not fire."));
		return;
	}

	SpawnTransform.SetRotation(LaunchVelocity.Rotation().Quaternion());
	CachedBowComponent->DrawServerFireDebug(SpawnLocation, ServerAimTarget);

	AArrowProjectile* Arrow = GetWorld()->SpawnActorDeferred<AArrowProjectile>(
		SpawnClass,
		SpawnTransform,
		Player,
		Player,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!Arrow)
	{
		return;
	}

	Arrow->IgnoreActorForMovement(Player);
	Arrow->IgnoreActorForMovement(CachedBow);

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		const float DrawAlpha = CachedBowComponent->GetDrawAlpha();
		const float ChargeDamageMultiplier = FMath::Lerp(MinChargeDamageMultiplier, MaxChargeDamageMultiplier, DrawAlpha);

		TSubclassOf<UGameplayEffect> DamageEffectClass = Arrow->GetDirectDamageEffectClass();
		if (!DamageEffectClass)
		{
			DamageEffectClass = UGASAttributeDamageGameplayEffect::StaticClass();
		}

		FStrengthDamageRequest DamageRequest;
		DamageRequest.SourceASC = ASC;
		DamageRequest.DamageEffectClass = DamageEffectClass;
		DamageRequest.AttackCoefficient = Arrow->GetAttackCoefficient();
		DamageRequest.ChargeMultiplier = ChargeDamageMultiplier;
		DamageRequest.InstigatorActor = Player;
		DamageRequest.EffectCauser = Arrow;
		DamageRequest.EffectLevel = Arrow->GetDirectDamageEffectLevel();
		Arrow->InitializeStrengthDamage(
			ASC,
			Player,
			UGASCombatLibrary::MakeStrengthDamageEffectSpec(DamageRequest));
	}

	Arrow->SetOwner(Player);
	Arrow->SetInstigator(Player);
	Arrow->FinishSpawning(SpawnTransform);
	Arrow->LaunchArrow(LaunchVelocity);
	CachedBow->Multicast_PlayReleaseFX();
	bHasFiredCurrentShot = true;
	if (CachedBowComponent)
	{
		CachedBowComponent->SetDrawAlpha(0.0f);
		CachedBowComponent->SetArrowNocked(false);
	}
}

bool UGA_BowAimFire::TryGetAimTargetFromPayload(const FGameplayEventData& Payload, FVector& OutAimTarget) const
{
	OutAimTarget = FVector::ZeroVector;
	if (Payload.TargetData.Num() == 0)
	{
		return false;
	}

	const FHitResult* HitResult = Payload.TargetData.Get(0)->GetHitResult();
	if (!HitResult)
	{
		return false;
	}

	OutAimTarget = !HitResult->ImpactPoint.IsNearlyZero() ? HitResult->ImpactPoint : HitResult->TraceEnd;
	return !OutAimTarget.IsNearlyZero();
}

void UGA_BowAimFire::FinishShot()
{
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
	ReleasePayload = FGameplayEventData();

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
	ReleasePayload = FGameplayEventData();

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

	CachedBow = Cast<ABowItem>(Player->EquippedItem);
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
