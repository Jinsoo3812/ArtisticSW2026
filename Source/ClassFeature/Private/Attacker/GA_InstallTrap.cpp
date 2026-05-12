// Fill out your copyright notice in the Description page of Project Settings.

#include "GA_InstallTrap.h"
#include "BasePlayer.h"
#include "BaseItem.h"
#include "GhostMeshActor.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"

UGA_InstallTrap::UGA_InstallTrap()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	bIsCurrentPositionValid = false;
}

void UGA_InstallTrap::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player || !IsValid(Player->EquippedItem))
	{
		UE_LOG(LogTemp, Warning, TEXT("UGA_InstallTrap::ActivateAbility : No Equipped Item. Cancel."));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// [로컬 클라이언트] 고스트 메쉬 생성 및 업데이트 시작
	if (Player->IsLocallyControlled())
	{
		if (GhostActorClass)
		{
			UE_LOG(LogTemp, Log, TEXT("UGA_InstallTrap::ActivateAbility : Spawning Ghost Actor for local preview."));	
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			SpawnedGhostActor = GetWorld()->SpawnActor<AGhostMeshActor>(GhostActorClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		}
		else UE_LOG(LogTemp, Warning, TEXT("UGA_InstallTrap::ActivateAbility : GhostActorClass is not set!"));

		// 주기적으로 트레이스 실행
		GetWorld()->GetTimerManager().SetTimer(TargetTimerHandle, this, &UGA_InstallTrap::UpdateInstallTarget, UpdateFrequency, true);
	}

	// 좌클릭 해제 대기 (즉발 설치)
	UAbilityTask_WaitInputRelease* WaitReleaseTask = UAbilityTask_WaitInputRelease::WaitInputRelease(this);
	if (WaitReleaseTask)
	{
		WaitReleaseTask->OnRelease.AddDynamic(this, &UGA_InstallTrap::OnInputReleased);
		WaitReleaseTask->ReadyForActivation();
	}

	// 우클릭 입력 대기 (취소)
	UAbilityTask_WaitGameplayEvent* WaitRightClickTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Key_Default_Mouse_RightClick);
	if (WaitRightClickTask)
	{
		WaitRightClickTask->EventReceived.AddDynamic(this, &UGA_InstallTrap::OnRightClickCancelled);
		WaitRightClickTask->ReadyForActivation();
	}
}

void UGA_InstallTrap::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	GetWorld()->GetTimerManager().ClearTimer(TargetTimerHandle);

	if (SpawnedGhostActor)
	{
		SpawnedGhostActor->Destroy();
		SpawnedGhostActor = nullptr;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_InstallTrap::UpdateInstallTarget()
{
	FVector HitLocation;
	FRotator HitRotation;

	bIsCurrentPositionValid = CheckInstallLocation(HitLocation, HitRotation);
	TargetTransform = FTransform(HitRotation, HitLocation);

	// 고스트 메쉬 위치 및 색상 갱신
	if (SpawnedGhostActor)
	{
		// 안 닿았을 경우 허공에라도 띄워주기 위해 끝점(TraceEnd) 사용
		FVector ShowLoc = HitLocation;
		if (HitLocation.IsNearlyZero())
		{
			ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
			ShowLoc = Player->GetPawnViewLocation() + (Player->GetBaseAimRotation().Vector() * InstallRange);
		}

		SpawnedGhostActor->SetActorLocationAndRotation(ShowLoc, HitRotation);
		SpawnedGhostActor->SetIsValidPosition(bIsCurrentPositionValid);
	}
}

bool UGA_InstallTrap::CheckInstallLocation(FVector& OutLocation, FRotator& OutRotation)
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player) return false;

	// GetPawnViewLocation과 GetBaseAimRotation은 서버에도 복제/보정되므로 안전
	FVector StartLoc = Player->GetPawnViewLocation();
	FVector AimDir = Player->GetBaseAimRotation().Vector();
	FVector EndLoc = StartLoc + (AimDir * InstallRange);

	FHitResult HitResult;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Player);
	if (SpawnedGhostActor)
	{
		Params.AddIgnoredActor(SpawnedGhostActor);
	}

	// 바닥이나 지형물만 검사할 수 있도록 적절한 채널(Visibility 또는 사용자 지정) 사용
	bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, StartLoc, EndLoc, ECC_Visibility, Params);

	if (bHit)
	{
		OutLocation = HitResult.ImpactPoint;

		// 1. 바닥의 기울기 60도 이하 여부 체크 (DotProduct 활용, cos(60) = 0.5)
		float SlopeDot = FVector::DotProduct(FVector::UpVector, HitResult.ImpactNormal);

		if (SlopeDot >= 0.5f)
		{
			// 설치 표면의 Normal에 맞춰 Z축 방향을 정렬
			OutRotation = FRotationMatrix::MakeFromZ(HitResult.ImpactNormal).Rotator();
			return true;
		}
	}

	OutLocation = FVector::ZeroVector;
	OutRotation = FRotator::ZeroRotator;
	return false;
}

void UGA_InstallTrap::OnInputReleased(float TimeHeld)
{
	if (!IsActive()) return;

	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player || !IsValid(Player->EquippedItem))
	{
		CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
		return;
	}

	// [서버 권한 실행] 
	// 네트워크 딜레이로 인해 클라의 고스트 메쉬 위치와 실제가 다를 수 있으므로 서버에서 최종 검사
	if (Player->HasAuthority())
	{
		FVector FinalLocation;
		FRotator FinalRotation;

		if (CheckInstallLocation(FinalLocation, FinalRotation))
		{
			UClass* SpawnClass = Player->EquippedItem->GetSpawnClass();

			if (SpawnClass)
			{
				FTransform SpawnTransform(FinalRotation, FinalLocation);

				// 함정 액터 스폰 (Deferred로 감싸서 추가 설정 후 FinishSpawning도 가능)
				AActor* TrapActor = GetWorld()->SpawnActor<AActor>(SpawnClass, SpawnTransform);
				if (TrapActor)
				{
					// 설치 성공 시 아이템 소비
					Player->UseEquippedItem(true);
					EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
					return;
				}
			}
		}

		// 실패 시 경고음/UI 로직을 여기에 추가할 수 있습니다.
		UE_LOG(LogTemp, Warning, TEXT("UGA_InstallTrap::OnInputReleased : Install Trap Failed: Invalid Position or SpawnClass."));
	}

	// 서버가 아니거나, 설치에 실패했다면 즉시 스킬 취소 (아이템 소모 안됨)
	CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
}

void UGA_InstallTrap::OnRightClickCancelled(FGameplayEventData Payload)
{
	if (IsActive())
	{
		CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
	}
}
