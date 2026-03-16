// Fill out your copyright notice in the Description page of Project Settings.


#include "ThrowItem.h"
#include "BasePlayer.h"
#include "BaseItem.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Components/PrimitiveComponent.h"
#include "BaseGameplayTags.h"

void UThrowItem::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
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
			UE_LOG(LogTemp, Warning, TEXT("UThrowItem::ActivateAbility : No Equipped Item. Throw Fail."));
			EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
			return;
		}

		// [클라이언트] 궤적 그리기 타이머 시작
		if (Player->IsLocallyControlled())
		{
			GetWorld()->GetTimerManager().SetTimer(TrajectoryTimerHandle, this, &UThrowItem::DrawTrajectory, TrajectoryFrequency, true);
		}
	}

	// 입력 대기 (좌클릭)
	UAbilityTask_WaitGameplayEvent* WaitConfirm = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		Input_MouseLeftClick,
		nullptr,
		false,
		false
	);

	// 좌클릭 이벤트 대기 (투척 확정)
	if (WaitConfirm)
	{
		WaitConfirm->EventReceived.AddDynamic(this, &UThrowItem::OnConfirmEventReceived);
		WaitConfirm->ReadyForActivation();
	}
}

void UThrowItem::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	// 종료 시 궤적 타이머 해제
	GetWorld()->GetTimerManager().ClearTimer(TrajectoryTimerHandle);

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UThrowItem::OnConfirmEventReceived(FGameplayEventData Payload)
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player) return;

	// [서버] Item을 손에서 분리한 후 투척
	if (Player->HasAuthority())
	{
		ABaseItem* ItemToThrow = Player->EquippedItem;

		if (IsValid(ItemToThrow))
		{
			// 서버와 클라이언트가 오차없이 공유하는 카메라의 현재 방향 벡터
			FVector LaunchDir = Player->GetBaseAimRotation().Vector();
			LaunchDir.Z += Upper; // 직구 방지 (살짝 들어올림)
			LaunchDir.Normalize();
			FVector LaunchVelocity = LaunchDir * ThrowSpeed;

			// 장착 Item을 손 및 해당 ItemSlot에서도 제거
			Player->UseEquippedItem(false);

			// 손에서 분리
			ItemToThrow->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

			// Item 투척
			ItemToThrow->OnThrown(LaunchVelocity, Player);
		}
	}

	// 투척 후 어빌리티 정상 종료
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UThrowItem::InputReleased(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	// 키를 떼었을 때 어빌리티를 취소 처리
	if (ActorInfo != nullptr && ActorInfo->AvatarActor != nullptr)
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
	}

	Super::InputReleased(Handle, ActorInfo, ActivationInfo);
}

void UThrowItem::DrawTrajectory()
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

	// 투척 초기 속도: 방향 * 힘
	FVector LaunchVelocity = LaunchDir * ThrowSpeed;

	// 가상의 궤적 표시 
	FPredictProjectilePathParams PredictParams(5.0f, StartLoc, LaunchVelocity, 3.0f, ECollisionChannel::ECC_Visibility, Player);
	PredictParams.DrawDebugType = EDrawDebugTrace::ForOneFrame;
	PredictParams.DrawDebugTime = TrajectoryFrequency;
	PredictParams.bTraceWithCollision = true;

	FPredictProjectilePathResult PredictResult;
	UGameplayStatics::PredictProjectilePath(Player, PredictParams, PredictResult);
}