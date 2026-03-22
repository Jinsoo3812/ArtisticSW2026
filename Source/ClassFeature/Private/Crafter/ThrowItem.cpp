// Fill out your copyright notice in the Description page of Project Settings.


#include "ThrowItem.h"
#include "BasePlayer.h"
#include "BaseItem.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
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

	// 입력 해제 대기
	UAbilityTask_WaitInputRelease* WaitReleaseTask = UAbilityTask_WaitInputRelease::WaitInputRelease(this);
	if (WaitReleaseTask)
	{
		WaitReleaseTask->OnRelease.AddDynamic(this, &UThrowItem::OnInputReleased);
		WaitReleaseTask->ReadyForActivation();
	}

	// 마우스 좌클릭(발사 확정) 대기
	UAbilityTask_WaitGameplayEvent* WaitClickTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Key_Default_Mouse_LeftClick);
	if (WaitClickTask)
	{
		WaitClickTask->EventReceived.AddDynamic(this, &UThrowItem::OnConfirmEventReceived);
		WaitClickTask->ReadyForActivation();
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

void UThrowItem::OnInputReleased(float TimeHeld)
{
	UE_LOG(LogTemp, Log, TEXT("UThrowItem: Skill Key Released. Canceling throw."));

	if (IsActive())
	{
		CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
	}
}

void UThrowItem::OnConfirmEventReceived(FGameplayEventData Payload)
{
	// 이미 취소되었거나 종료되었다면 무시
	if (!IsActive()) return;

	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player) return;

	// [서버] Item을 손에서 분리한 후 투척
	if (Player->HasAuthority())
	{
		ABaseItem* ItemToThrow = Player->EquippedItem;

		if (IsValid(ItemToThrow))
		{
			FVector LaunchDir = Player->GetBaseAimRotation().Vector();
			LaunchDir.Z += Upper;
			LaunchDir.Normalize();
			FVector LaunchVelocity = LaunchDir * ThrowSpeed;

			Player->UseEquippedItem(false);
			ItemToThrow->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			ItemToThrow->OnThrown(LaunchVelocity, Player);
		}
	}

	// 투척 후 어빌리티 정상 종료
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
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