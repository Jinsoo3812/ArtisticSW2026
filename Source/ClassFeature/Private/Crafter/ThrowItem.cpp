// Fill out your copyright notice in the Description page of Project Settings.


#include "ThrowItem.h"
#include "BasePlayer.h"
#include "BaseGameplayTags.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "Item/BaseItem.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/ProjectileMovementComponent.h"

void UThrowItem::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo())) {
		if (!Player->EquippedItem.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("투척 실패: 장착된 아이템이 없습니다."));
			EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
			return;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("투척 시작: 조준 모드로 진입합니다."));
	bIsConfirmed = false; // 초기화
	StartAiming();

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
	// 궤적 그리기 타이머 정리
	GetWorld()->GetTimerManager().ClearTimer(TrajectoryTimerHandle);

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UThrowItem::StartAiming()
{
	// 시전자 클라이언트에만 궤적을 그림
	if (GetActorInfo().IsLocallyControlled())
	{
		GetWorld()->GetTimerManager().SetTimer(TrajectoryTimerHandle, this, &UThrowItem::DrawTrajectory, TrajectoryFrequency, true);
	}
}

bool UThrowItem::TraceUnderCrosshairs(FHitResult& OutHitResult, float TraceDistance)
{
	APlayerController* PC = Cast<APlayerController>(GetActorInfo().PlayerController.Get());
	if (!PC || !PC->PlayerCameraManager) return false;

	FVector Start = PC->PlayerCameraManager->GetCameraLocation();
	FVector ForwardDir = PC->PlayerCameraManager->GetCameraRotation().Vector();
	FVector End = Start + (ForwardDir * TraceDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetAvatarActorFromActorInfo());

	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (Player && Player->EquippedItem.IsValid())
	{
		QueryParams.AddIgnoredActor(Player->EquippedItem.Get());
	}

	bool bHit = GetWorld()->LineTraceSingleByChannel(OutHitResult, Start, End, ECC_Visibility, QueryParams);
	if (!bHit)
	{
		OutHitResult.Location = End;
	}

	return true;
}

void UThrowItem::DrawTrajectory()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player) return;

	// 발사 시작 위치는 Item이 달려있는 현재 위치
	FVector StartLocation = Player->GetActorLocation() + FVector(0, 0, 50.f); // 실패 시 임시 위치
	if (Player->EquippedItem.IsValid())
	{
		StartLocation = Player->EquippedItem.Get()->GetActorLocation();
	}

	// 카메라가 바라보는 방향으로 목표 지점 트레이스
	FHitResult HitResult;
	TraceUnderCrosshairs(HitResult, 5000.f);
	FVector EndLocation = HitResult.Location;

	// 목표 지점을 향한 발사 벡터(Velocity) 계산
	FVector LaunchVelocity;
	bool bHasValidTrajectory = UGameplayStatics::SuggestProjectileVelocity_CustomArc(
		this,
		LaunchVelocity,
		StartLocation,
		EndLocation,
		GetWorld()->GetGravityZ(),
		0.5f // 포물선 곡률 높이 (시간)
	);

	if (bHasValidTrajectory)
	{
		// 포물선 궤적 예측 및 디버그 라인 그리기
		FPredictProjectilePathParams PredictParams(
			5.f, // 투사체 반경
			StartLocation,
			LaunchVelocity,
			2.0f, // 시뮬레이션 최대 시간
			ECollisionChannel::ECC_Visibility,
			Player
		);
		PredictParams.DrawDebugType = EDrawDebugTrace::ForOneFrame; // 매 프레임 갱신

		FPredictProjectilePathResult PredictResult;
		UGameplayStatics::PredictProjectilePath(GetWorld(), PredictParams, PredictResult);
	}
}

void UThrowItem::OnConfirmEventReceived(FGameplayEventData Payload)
{
	// 발사 확정 처리
	bIsConfirmed = true;

	// 서버는 클라이언트가 보낸 TargetData를 받을 준비
	GetAbilitySystemComponentFromActorInfo()->AbilityTargetDataSetDelegate(
		CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey()
	).AddUObject(this, &UThrowItem::OnTargetDataReadyCallback);

	// [클라이언트] 발사
	if (GetActorInfo().IsLocallyControlled())
	{
		// 조준 중지
		GetWorld()->GetTimerManager().ClearTimer(TrajectoryTimerHandle);

		FHitResult HitResult;
		TraceUnderCrosshairs(HitResult, 5000.f);

		// TargetLocation을 TargetData로 포장하여 네트워크 최적화
		FGameplayAbilityTargetData_LocationInfo* TargetData = new FGameplayAbilityTargetData_LocationInfo();
		TargetData->TargetLocation.LocationType = EGameplayAbilityTargetingLocationType::LiteralTransform;
		TargetData->TargetLocation.LiteralTransform = FTransform(HitResult.Location);

		FGameplayAbilityTargetDataHandle TargetDataHandle;
		TargetDataHandle.Add(TargetData);

		GetAbilitySystemComponentFromActorInfo()->CallServerSetReplicatedTargetData(
			CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey(), TargetDataHandle, FGameplayTag(), GetAbilitySystemComponentFromActorInfo()->ScopedPredictionKey);
	}
}

void UThrowItem::OnTargetDataReadyCallback(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag)
{
	// 캐시 데이터 및 바인딩 정리
	GetAbilitySystemComponentFromActorInfo()->ConsumeClientReplicatedTargetData(CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());

	// [서버] 실제 투척 및 스폰은 서버에서만 진행
	if (HasAuthority(&CurrentActivationInfo))
	{
		ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
		if (Player && Data.Data.Num() > 0 && ReplicatedProjectileClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("Server received TargetData with %d entries"), Data.Data.Num());

			FGameplayAbilityTargetData_LocationInfo* LocationInfo = static_cast<FGameplayAbilityTargetData_LocationInfo*>(Data.Data[0].Get());
			FVector TargetLocation = LocationInfo->TargetLocation.LiteralTransform.GetLocation();
			FVector StartLocation = Player->EquippedItem.IsValid() ? Player->EquippedItem->GetActorLocation() : Player->GetActorLocation() + FVector(0, 0, 50.f);

			// 서버가 클라이언트가 넘겨준 타겟 지점을 바탕으로 발사 벡터를 직접 계산
			FVector LaunchVelocity;
			UGameplayStatics::SuggestProjectileVelocity_CustomArc(
				this, LaunchVelocity, StartLocation, TargetLocation, GetWorld()->GetGravityZ(), 0.5f);

			// 복제되는 투사체 스폰
			FActorSpawnParameters SpawnParams;
			SpawnParams.Instigator = Player;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			AActor* Projectile = GetWorld()->SpawnActor<AActor>(ReplicatedProjectileClass, StartLocation, LaunchVelocity.Rotation(), SpawnParams);

			if (Projectile)
			{
				Projectile->SetNetUpdateFrequency(ProjectileNetUpdateFrequency);


				// 투사체 컴포넌트에 서버가 계산한 속도 주입
				UProjectileMovementComponent* ProjComp = Projectile->FindComponentByClass<UProjectileMovementComponent>();
				if (ProjComp)
				{
					ProjComp->Velocity = LaunchVelocity;
				}
			}

			// 아이템 사용 처리
			Player->UseEquippedItem();
		}
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UThrowItem::InputReleased(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputReleased(Handle, ActorInfo, ActivationInfo);

	UE_LOG(LogTemp, Warning, TEXT("Input Released"));

	// 발사가 이미 확정된 상태가 아니라면 스킬을 취소
	if (!bIsConfirmed)
	{
		// Cancle로 처리
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
	}
}