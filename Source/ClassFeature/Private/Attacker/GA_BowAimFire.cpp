#include "Attacker/GA_BowAimFire.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "Animation/AnimInstance.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Item/Components/BowComponent.h"
#include "Item/Projectiles/ArrowProjectile.h"
#include "Item/Weapons/BowItem.h"

UGA_BowAimFire::UGA_BowAimFire()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
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

	UAbilityTask_WaitInputRelease* WaitRightReleaseTask = UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
	if (WaitRightReleaseTask)
	{
		WaitRightReleaseTask->OnRelease.AddDynamic(this, &UGA_BowAimFire::OnRightClickReleased);
		WaitRightReleaseTask->ReadyForActivation();
	}

	UAbilityTask_WaitGameplayEvent* WaitLeftPressedTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Key_Default_Mouse_LeftClick, nullptr, true, true);
	if (WaitLeftPressedTask)
	{
		WaitLeftPressedTask->EventReceived.AddDynamic(this, &UGA_BowAimFire::OnLeftClickPressed);
		WaitLeftPressedTask->ReadyForActivation();
	}

	UAbilityTask_WaitGameplayEvent* WaitLeftReleasedTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Key_Default_Mouse_LeftClick_Released, nullptr, true, true);
	if (WaitLeftReleasedTask)
	{
		WaitLeftReleasedTask->EventReceived.AddDynamic(this, &UGA_BowAimFire::OnLeftClickReleased);
		WaitLeftReleasedTask->ReadyForActivation();
	}

	UAbilityTask_WaitGameplayEvent* WaitFireArrowTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Event_Montage_FireArrow, nullptr, true, true);
	if (WaitFireArrowTask)
	{
		WaitFireArrowTask->EventReceived.AddDynamic(this, &UGA_BowAimFire::OnReleaseFireEvent);
		WaitFireArrowTask->ReadyForActivation();
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
	if (!IsActive() || !CachedBowComponent || bIsDrawing || bIsFullyDrawn || bIsReleaseInProgress)
	{
		return;
	}

	bIsDrawing = true;
	bIsFullyDrawn = false;
	bIsReleaseInProgress = false;
	bHasFiredCurrentShot = false;
	DrawStartTime = GetWorld()->GetTimeSeconds();
	CachedBowComponent->SetDrawAlpha(0.0f);

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(State_Attacking);
	}
	SetBowDrawTagState(true, false, false);

	if (ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo()))
	{
		Player->StopSprint();
	}

	PlayDrawMontage();

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
		BeginRelease();
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

	const float HeldTime = GetWorld()->GetTimeSeconds() - DrawStartTime;
	const float DrawDuration = FMath::Max(FullDrawTime - DrawAlphaStartDelay, KINDA_SMALL_NUMBER);
	const float DrawAlpha = FMath::Clamp((HeldTime - DrawAlphaStartDelay) / DrawDuration, 0.0f, 1.0f);
	CachedBowComponent->SetDrawAlpha(DrawAlpha);

	if (DrawAlpha >= FullDrawAlphaToRelease)
	{
		GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);
		CachedBowComponent->SetDrawAlpha(1.0f);
		StopDrawMontage(DrawMontageBlendOutTime);
		SetBowDrawTagState(false, true, false);
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

void UGA_BowAimFire::BeginRelease()
{
	if (!IsActive() || !CachedBowComponent || bIsReleaseInProgress || bHasFiredCurrentShot)
	{
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);
	bIsDrawing = false;
	bIsFullyDrawn = true;
	bIsReleaseInProgress = true;
	CachedBowComponent->SetDrawAlpha(1.0f);
	SetBowDrawTagState(false, true, true);
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
				FireArrow();
			}
			return;
		}
	}

	FireArrow();
	FinishShot();
}

void UGA_BowAimFire::OnReleaseFireEvent(FGameplayEventData Payload)
{
	if (!IsActive() || !bIsReleaseInProgress || !bIsFullyDrawn || bHasFiredCurrentShot)
	{
		return;
	}

	FireArrow();
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

void UGA_BowAimFire::FireArrow()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (bHasFiredCurrentShot || !bIsFullyDrawn || !Player || !Player->HasAuthority() || !CachedBow || !CachedBowComponent)
	{
		return;
	}

	UClass* SpawnClass = CachedBow->GetSpawnClass();
	if (!SpawnClass || !SpawnClass->IsChildOf(AArrowProjectile::StaticClass()))
	{
		UE_LOG(LogTemp, Warning, TEXT("UGA_BowAimFire::FireArrow : Bow SpawnClass must derive from AArrowProjectile."));
		return;
	}

	const FVector LaunchDirection = Player->GetBaseAimRotation().Vector();
	const float FireSpeed = CachedBowComponent->GetCurrentFireSpeed();
	const FVector LaunchVelocity = LaunchDirection * FireSpeed;

	FTransform SpawnTransform = CachedBowComponent->BuildArrowSpawnTransform();
	SpawnTransform.SetRotation(LaunchDirection.Rotation().Quaternion());

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

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		const float DrawAlpha = CachedBowComponent->GetDrawAlpha();
		const float ChargeDamageMultiplier = FMath::Lerp(MinChargeDamageMultiplier, MaxChargeDamageMultiplier, DrawAlpha);
		Arrow->InitializeDamage(ASC, Player, ChargeDamageMultiplier);
	}

	Arrow->SetOwner(Player);
	Arrow->SetInstigator(Player);
	Arrow->FinishSpawning(SpawnTransform);
	Arrow->LaunchArrow(LaunchVelocity);
	CachedBow->Multicast_PlayReleaseFX();
	bHasFiredCurrentShot = true;
}

void UGA_BowAimFire::FinishShot()
{
	GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);
	StopDrawMontage(DrawMontageBlendOutTime);

	bIsDrawing = false;
	bIsFullyDrawn = false;
	bIsReleaseInProgress = false;
	bHasFiredCurrentShot = false;

	if (CachedBowComponent)
	{
		CachedBowComponent->SetDrawAlpha(0.0f);
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(State_Attacking);
	}
	RemoveBowStateTags();
}

void UGA_BowAimFire::ResetBowState()
{
	GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);
	StopDrawMontage(DrawMontageBlendOutTime);
	bIsDrawing = false;
	bIsFullyDrawn = false;
	bIsReleaseInProgress = false;
	bHasFiredCurrentShot = false;

	if (CachedBowComponent)
	{
		CachedBowComponent->SetDrawAlpha(0.0f);
		CachedBowComponent->SetAiming(false);
	}
	RemoveBowStateTags();
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
