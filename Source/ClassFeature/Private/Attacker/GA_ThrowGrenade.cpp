// Fill out your copyright notice in the Description page of Project Settings.

#include "GA_ThrowGrenade.h"
#include "BasePlayer.h"
#include "BaseItem.h"
#include "Projectiles/GrenadeProjectile.h"
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

	// 입력 대기 (좌클릭)
	UAbilityTask_WaitGameplayEvent* WaitCancle = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		Input_MouseRightClick,
		nullptr,
		false,
		false
	);

	// 좌클릭 이벤트 대기 (투척 확정)
	if (WaitCancle)
	{
		WaitCancle->EventReceived.AddDynamic(this, &UGA_ThrowGrenade::OnCancelEventReceived);
		WaitCancle->ReadyForActivation();
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

void UGA_ThrowGrenade::InputReleased(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputReleased(Handle, ActorInfo, ActivationInfo);

	// 이미 우클릭으로 취소되어 어빌리티가 종료된 상태라면 투척 로직을 무시
	if (!IsActive())
	{
		return;
	}

	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();

	if (!Player || !ASC || !IsValid(Player->EquippedItem))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// [서버] 수류탄 스폰 및 투척
	if (Player->HasAuthority())
	{
		// EquippedItem에서 SpawnClass를 가져오고 형변환 체크
		UClass* SpawnClass = Player->EquippedItem->GetSpawnClass();
		UStaticMesh* ItemMesh = Player->EquippedItem->GetStaticMesh();

		if (!SpawnClass || !SpawnClass->IsChildOf(AGrenadeProjectile::StaticClass()))
		{
			EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
			return;
		}

		// 서버와 클라이언트가 오차 없이 공유하는 카메라의 현재 방향 벡터
		FVector LaunchDir = Player->GetBaseAimRotation().Vector();
		LaunchDir.Z += Upper; // 직구 방지
		LaunchDir.Normalize();
		FVector LaunchVelocity = LaunchDir * ThrowSpeed;

		// 스폰 위치 (손 소켓 기준)
		FVector SpawnLocation = Player->GetActorLocation();
		if (Player->GetMesh())
		{
			SpawnLocation = Player->GetMesh()->GetSocketLocation(FName("hand_r"));
		}

		// 회전값은 발사 방향에 맞춰줌
		FTransform SpawnTransform(LaunchVelocity.Rotation(), SpawnLocation);

		// 데미지 GE Spec 생성
		FGameplayEffectSpecHandle DamageSpecHandle;
		if (DamageEffectClass)
		{
			FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
			ContextHandle.AddInstigator(Player, Player);
			DamageSpecHandle = ASC->MakeOutgoingSpec(DamageEffectClass, 1.0f, ContextHandle);
		}

		// 지연 생성
		AGrenadeProjectile* Grenade = GetWorld()->SpawnActorDeferred<AGrenadeProjectile>(
			SpawnClass, SpawnTransform, Player, Player, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

		if (Grenade)
		{
			Grenade->SetInstigator(Player);
			Grenade->SetOwner(Player);
			Grenade->DamageEffectSpecHandle = DamageSpecHandle;

			Grenade->SetGrenadeMesh(ItemMesh);

			Grenade->FinishSpawning(SpawnTransform);

			// 3. 스폰 직후 단순 물리 힘을 가해 던짐
			Grenade->LaunchProjectile(LaunchVelocity);
		}

		// 장착 Item(들고 있던 수류탄) 소비 및 파괴 처리 (true)
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

void UGA_ThrowGrenade::OnCancelEventReceived(FGameplayEventData Payload)
{
	// 마우스 우클릭 이벤트 수신 시 어빌리티 취소 처리
	CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
}