#include "Attacker/GA_Sniping.h"
#include "BasePlayer.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputPress.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Attacker/AbilityTask_RecoilInterpolation.h"
#include "BaseGameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraComponent.h"

UGA_Sniping::UGA_Sniping()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	bIsSnipingActive = false;
	bIsRecoiling = false;
	
	// State_Sniping 태그를 부여하여 BasePlayer에서 카메라 줌인 등을 처리
	ActivationOwnedTags.AddTag(State_Sniping);
}

void UGA_Sniping::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	bIsSnipingActive = true;
	bIsRecoiling = false;

	// 우클릭(조준키) 재입력 대기 (토글 해제)
	UAbilityTask_WaitInputPress* WaitRightClick = UAbilityTask_WaitInputPress::WaitInputPress(this);
	if (WaitRightClick)
	{
		WaitRightClick->OnPress.AddDynamic(this, &UGA_Sniping::OnRightClick);
		WaitRightClick->ReadyForActivation();
	}

	// 사격 입력 대기 (좌클릭)
	UAbilityTask_WaitGameplayEvent* WaitShoot = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Key_Default_Mouse_LeftClick);
	if (WaitShoot)
	{
		WaitShoot->EventReceived.AddDynamic(this, &UGA_Sniping::OnShoot);
		WaitShoot->ReadyForActivation();
	}

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UGA_Sniping::OnRightClick(float TimeWaited)
{
	// 우클릭 재입력 시 어빌리티 종료 (토글 해제)
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

void UGA_Sniping::OnShoot(FGameplayEventData Payload)
{
	if (bIsRecoiling)
	{
		return;
	}

	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player || !Player->GetFollowCamera())
	{
		return;
	}

	// 로컬/클라이언트에서 트레이스 수행
	if (Player->IsLocallyControlled())
	{
		FVector StartLoc = Player->GetFollowCamera()->GetComponentLocation();
		FVector ForwardVector = Player->GetFollowCamera()->GetForwardVector();
		FVector EndLoc = StartLoc + (ForwardVector * TraceDistance);

		FHitResult HitResult;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(Player);

		// ECC_Visibility 채널을 사용하여 장애물 및 타겟 검출
		bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, StartLoc, EndLoc, ECC_Visibility, QueryParams);

		if (bHit)
		{
			// 타겟 명중 시 서버로 데이터 전송
			if (HitResult.GetActor())
			{
				Server_ProcessHit(HitResult);
			}
		}

		// 반동 태스크 시작
		bIsRecoiling = true;
		UAbilityTask_RecoilInterpolation* RecoilTask = UAbilityTask_RecoilInterpolation::RecoilInterpolation(
			this, RecoilPitchOffset, RecoilAscentTime, RecoilRecoveryTime, RecoilRestingRadius);

		if (RecoilTask)
		{
			RecoilTask->OnRecoilFinished.AddDynamic(this, &UGA_Sniping::OnRecoilFinished);
			RecoilTask->ReadyForActivation();
		}
	}
}

void UGA_Sniping::Server_ProcessHit_Implementation(const FHitResult& HitResult)
{
	// 서버에서 유효성 검증 및 데미지 적용
	AActor* HitActor = HitResult.GetActor();
	if (HitActor && DamageEffectClass)
	{
		UAbilitySystemComponent* TargetASC = HitActor->FindComponentByClass<UAbilitySystemComponent>();
		if (TargetASC)
		{
			UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
			FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
			ContextHandle.AddInstigator(GetAvatarActorFromActorInfo(), GetAvatarActorFromActorInfo());
			ContextHandle.AddHitResult(HitResult);
			
			SourceASC->ApplyGameplayEffectToTarget(DamageEffectClass.GetDefaultObject(), TargetASC, 1.0f, ContextHandle);
			UE_LOG(LogTemp, Log, TEXT("UGA_Sniping: Hit Success -> Applied damage to %s"), *HitActor->GetName());
		}
	}
}

void UGA_Sniping::OnRecoilFinished()
{
	bIsRecoiling = false;
}

void UGA_Sniping::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	bIsSnipingActive = false;
	bIsRecoiling = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
