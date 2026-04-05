// Fill out your copyright notice in the Description page of Project Settings.

#include "GA_ThrowGrenade.h"
#include "BasePlayer.h"
#include "BaseItem.h"
#include "Projectiles/GrenadeProjectile.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h" // 몽타주 재생 태스크 추가
#include "AbilitySystemComponent.h"
#include "Kismet/GameplayStatics.h"
#include "BaseGameplayTags.h"

void UGA_ThrowGrenade::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (Player)
	{
		if (!IsValid(Player->EquippedItem))
		{
			UE_LOG(LogTemp, Warning, TEXT("UGA_ThrowGrenade::ActivateAbility : No Equipped Item. Cancel."));
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

		if (Player->IsLocallyControlled())
		{
			GetWorld()->GetTimerManager().SetTimer(TrajectoryTimerHandle, this, &UGA_ThrowGrenade::DrawTrajectory, TrajectoryFrequency, true);
		}
	}

	// 1. [핵심 변경] 어빌리티가 시작되자마자 몽타주를 재생합니다. (Start -> Ready 루프 무한 대기)
	if (ThrowMontage)
	{
		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, ThrowMontage, 1.0f);

		if (MontageTask)
		{
			MontageTask->OnBlendOut.AddDynamic(this, &UGA_ThrowGrenade::OnMontageCompleted);
			MontageTask->OnInterrupted.AddDynamic(this, &UGA_ThrowGrenade::OnMontageCompleted);
			MontageTask->OnCancelled.AddDynamic(this, &UGA_ThrowGrenade::OnMontageCompleted);
			MontageTask->OnCompleted.AddDynamic(this, &UGA_ThrowGrenade::OnMontageCompleted);
			MontageTask->ReadyForActivation();
		}

		// 몽타주 내부의 "발사!" 노티파이 대기도 시작부터 같이 켜둡니다.
		UAbilityTask_WaitGameplayEvent* WaitEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, ThrowEventTag);
		if (WaitEventTask)
		{
			WaitEventTask->EventReceived.AddDynamic(this, &UGA_ThrowGrenade::OnThrowEventReceived);
			WaitEventTask->ReadyForActivation();
		}
	}

	// 2. 좌클릭 해제 대기 (던지기 섹션으로 점프하기 위함)
	UAbilityTask_WaitInputRelease* WaitReleaseTask = UAbilityTask_WaitInputRelease::WaitInputRelease(this);
	if (WaitReleaseTask)
	{
		WaitReleaseTask->OnRelease.AddDynamic(this, &UGA_ThrowGrenade::OnInputReleased);
		WaitReleaseTask->ReadyForActivation();
	}

	// 3. 우클릭 입력 대기 (스킬 취소)
	UAbilityTask_WaitGameplayEvent* WaitRightClickTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Key_Default_Mouse_RightClick);
	if (WaitRightClickTask)
	{
		WaitRightClickTask->EventReceived.AddDynamic(this, &UGA_ThrowGrenade::OnRightClickCancelled);
		WaitRightClickTask->ReadyForActivation();
	}
}

void UGA_ThrowGrenade::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// 종료 시 궤적 타이머 해제
	GetWorld()->GetTimerManager().ClearTimer(TrajectoryTimerHandle);

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(State_Aiming);
		ASC->RemoveLooseGameplayTag(State_Attacking); // 어빌리티 종료 시 회전 고정도 해제
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_ThrowGrenade::OnInputReleased(float TimeHeld)
{
	if (!IsActive()) return;

	// 화면의 궤적 지우기
	GetWorld()->GetTimerManager().ClearTimer(TrajectoryTimerHandle);

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();

	// 몽타주가 세팅되어 있다면 "Throw" 섹션으로 강제 점프!
	if (ThrowMontage && ASC)
	{
		// 루프를 돌며 폼을 잡고 있던 애니메이션이 던지는 모션으로 즉시 넘어갑니다.
		ASC->CurrentMontageJumpToSection(ThrowSectionName);

		// 1. 조준 태그를 빼서 카메라는 즉시 줌아웃 시킵니다.
		ASC->RemoveLooseGameplayTag(State_Aiming);

		// 2. 공격 태그를 넣어서 애니메이션이 끝날 때까지 캐릭터 회전을 고정합니다.
		ASC->AddLooseGameplayTag(State_Attacking);

		ASC->CurrentMontageJumpToSection(ThrowSectionName);
	}
	else
	{
		// 몽타주가 없을 때의 안전 장치
		FGameplayEventData EmptyData;
		OnThrowEventReceived(EmptyData);
		OnMontageCompleted();
	}
}

void UGA_ThrowGrenade::OnThrowEventReceived(FGameplayEventData Payload)
{
	// 기존의 OnInputReleased에 있던 수류탄 실제 스폰/발사 로직입니다.
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();

	if (!Player || !ASC || !IsValid(Player->EquippedItem))
	{
		return;
	}

	// [서버] 수류탄 스폰 및 투척
	if (Player->HasAuthority())
	{
		UClass* SpawnClass = Player->EquippedItem->GetSpawnClass();
		UStaticMesh* ItemMesh = Player->EquippedItem->GetStaticMesh();

		if (!SpawnClass || !SpawnClass->IsChildOf(AGrenadeProjectile::StaticClass()))
		{
			return;
		}

		FVector LaunchDir = Player->GetBaseAimRotation().Vector();
		LaunchDir.Z += Upper;
		LaunchDir.Normalize();
		FVector LaunchVelocity = LaunchDir * ThrowSpeed;

		FVector SpawnLocation = Player->EquippedItem->GetActorLocation();

		FTransform SpawnTransform(LaunchVelocity.Rotation(), SpawnLocation);

		FGameplayEffectSpecHandle DamageSpecHandle;
		if (DamageEffectClass)
		{
			FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
			ContextHandle.AddInstigator(Player, Player);
			DamageSpecHandle = ASC->MakeOutgoingSpec(DamageEffectClass, 1.0f, ContextHandle);
		}

		AGrenadeProjectile* Grenade = GetWorld()->SpawnActorDeferred<AGrenadeProjectile>(
			SpawnClass, SpawnTransform, Player, Player, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

		if (Grenade)
		{
			Grenade->SetInstigator(Player);
			Grenade->SetOwner(Player);
			Grenade->DamageEffectSpecHandle = DamageSpecHandle;
			Grenade->SetGrenadeMesh(ItemMesh);
			Grenade->FinishSpawning(SpawnTransform);
			Grenade->LaunchProjectile(LaunchVelocity);
		}

		// 장착 Item 소비 및 파괴
		Player->UseEquippedItem(true);
	}
}

void UGA_ThrowGrenade::OnMontageCompleted()
{
	// 애니메이션이 완전히 끝나면 어빌리티를 정상 종료합니다.
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGA_ThrowGrenade::DrawTrajectory()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player || !IsValid(Player->EquippedItem)) return;

	// 투척 시작 위치
	FVector StartLoc = Player->EquippedItem->GetActorLocation();

	// 카메라가 보고 있는 방향 벡터
	FVector LaunchDir = Player->GetBaseAimRotation().Vector();

	// 직구가 안되게 살짝 들어올림
	LaunchDir.Z += Upper;
	LaunchDir.Normalize();

	// 투척 초기 속도
	FVector LaunchVelocity = LaunchDir * ThrowSpeed;

	// 가상의 궤적 표시
	FPredictProjectilePathParams PredictParams(5.0f, StartLoc, LaunchVelocity, 3.0f, ECollisionChannel::ECC_Visibility, Player);
	PredictParams.DrawDebugType = EDrawDebugTrace::ForOneFrame;
	PredictParams.DrawDebugTime = TrajectoryFrequency;
	PredictParams.bTraceWithCollision = true;

	FPredictProjectilePathResult PredictResult;
	UGameplayStatics::PredictProjectilePath(Player, PredictParams, PredictResult);
}

void UGA_ThrowGrenade::OnRightClickCancelled(FGameplayEventData Payload)
{
	UE_LOG(LogTemp, Log, TEXT("UGA_ThrowGrenade: Right Click Event Received. Canceling Ability."));

	// 이미 취소되었거나 종료되었다면 무시
	if (!IsActive()) return;

	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player) return;

	// [서버]
	if (Player->HasAuthority())
	{
		CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
	}
}