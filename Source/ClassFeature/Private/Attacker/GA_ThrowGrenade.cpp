// Fill out your copyright notice in the Description page of Project Settings.

#include "GA_ThrowGrenade.h"
#include "BasePlayer.h"
#include "BaseItem.h"
#include "Projectiles/GrenadeProjectile.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
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
		// 손에 든 물건이 있는지 확인
		if (!IsValid(Player->EquippedItem))
		{
			UE_LOG(LogTemp, Warning, TEXT("UGA_ThrowGrenade::ActivateAbility : No Equipped Item. Cancel."));
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

		// [클라이언트] 궤적 그리기 타이머 시작
		if (Player->IsLocallyControlled())
		{
			GetWorld()->GetTimerManager().SetTimer(TrajectoryTimerHandle, this, &UGA_ThrowGrenade::DrawTrajectory, TrajectoryFrequency, true);
		}
	}

	// 좌클릭 해제(투척 확정) 대기
	UAbilityTask_WaitInputRelease* WaitReleaseTask = UAbilityTask_WaitInputRelease::WaitInputRelease(this);
	if (WaitReleaseTask)
	{
		WaitReleaseTask->OnRelease.AddDynamic(this, &UGA_ThrowGrenade::OnInputReleased);
		WaitReleaseTask->ReadyForActivation();
	}

	// 우클릭 입력(스킬 취소) 대기
	UAbilityTask_WaitGameplayEvent* WaitRightClickTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Key_Default_Mouse_RightClick);
	if (WaitRightClickTask)
	{
		WaitRightClickTask->EventReceived.AddDynamic(this, &UGA_ThrowGrenade::OnRightClickCancelled);
		WaitRightClickTask->ReadyForActivation();
	}
}

void UGA_ThrowGrenade::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	// 종료 시 궤적 타이머 해제
	GetWorld()->GetTimerManager().ClearTimer(TrajectoryTimerHandle);

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_ThrowGrenade::OnInputReleased(float TimeHeld)
{
	// 어빌리티가 이미 취소되었거나 종료되었다면 무시
	if (!IsActive())
	{
		return;
	}

	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();

	if (!Player || !ASC || !IsValid(Player->EquippedItem))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	// [서버] 수류탄 스폰 및 투척
	if (Player->HasAuthority())
	{
		UClass* SpawnClass = Player->EquippedItem->GetSpawnClass();
		UStaticMesh* ItemMesh = Player->EquippedItem->GetStaticMesh();

		if (!SpawnClass || !SpawnClass->IsChildOf(AGrenadeProjectile::StaticClass()))
		{
			EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
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

	// 투척 후 어빌리티 정상 종료
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