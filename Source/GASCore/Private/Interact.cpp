// Fill out your copyright notice in the Description page of Project Settings.

#include "Interact.h"
#include "Interactable.h"
#include "BaseGameplayTags.h"
#include "CollisionChannels.h"
#include "Camera/CameraComponent.h"
#include "AbilitySystemComponent.h"
#include "DrawDebugHelpers.h"

void UInteract::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	bool bIsLocallyControlled = ActorInfo->IsLocallyControlled();
	bool bHasAuthority = HasAuthority(&ActivationInfo);

	FGameplayAbilityTargetDataHandle TargetDataHandle;

	// [클라] 로컬에서 Trace 수행 후 서버로 TargetData 전송
	if (bIsLocallyControlled)
	{
		// Trace 수행
		FHitResult HitResult;
		PerformLocalTrace(HitResult);

		// Trace 결과를 TargetData로 패키징
		if (HitResult.bBlockingHit)
		{
			FGameplayAbilityTargetData_SingleTargetHit* TargetData = new FGameplayAbilityTargetData_SingleTargetHit();
			TargetData->HitResult = HitResult;
			TargetDataHandle.Add(TargetData);
		}

		// 서버로 TargetData 전송
		ASC->CallServerSetReplicatedTargetData(
			Handle,
			ActivationInfo.GetActivationPredictionKey(),
			TargetDataHandle,
			FGameplayTag(),
			ASC->ScopedPredictionKey
		);
	}

	// [서버] 클라이언트가 보낸 TargetData를 수신하기 위해 델리게이트 바인딩
	if (bHasAuthority)
	{
		// Test 환경 (Statd alone)에서는 통신없이 즉시 처리
		if (bIsLocallyControlled)
		{
			ProcessInteract(TargetDataHandle);
		}
		else
		{
			// 실제 환경 (Dedicated) 에서는 클라이언트가 보낸 데이터를 기다려야 하므로 델리게이트 바인딩
			ASC->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey()).AddUObject(this, &UInteract::OnTargetDataReadyCallback);

			// 이미 도착한 데이터가 있다면 델리게이트를 즉시 실행
			ASC->CallReplicatedTargetDataDelegatesIfSet(Handle, ActivationInfo.GetActivationPredictionKey());
		}
	}

	// [클라] 로컬은 데이터를 보냈으므로 예측 단계에서 어빌리티 종료 
	if (bIsLocallyControlled && !bHasAuthority)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	}
}

void UInteract::PerformLocalTrace(FHitResult& OutHitResult)
{
	AActor* Caster = Cast<AActor>(GetAvatarActorFromActorInfo());
	if (!Caster) return;

	UCameraComponent* CameraComp = Caster->FindComponentByClass<UCameraComponent>();
	UWorld* World = Caster->GetWorld();
	if (!CameraComp || !World) return;

	FVector StartLoc = Caster->GetActorLocation();
	FVector EndLoc = StartLoc + (CameraComp->GetForwardVector() * InteractTraceDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Caster); // 자기 자신 스캔 제외

	World->SweepSingleByChannel(
		OutHitResult,
		StartLoc,
		EndLoc,
		FQuat::Identity,
		ECC_Interactable,
		FCollisionShape::MakeSphere(InteractTraceRadius),
		QueryParams
	);

#if ENABLE_DRAW_DEBUG
	FColor DrawColor = OutHitResult.bBlockingHit ? FColor::Green : FColor::Red;
	FVector TraceCenter = StartLoc + (EndLoc - StartLoc) * 0.5f;
	float TraceHalfHeight = (EndLoc - StartLoc).Size() * 0.5f;
	FQuat TraceRotation = FRotationMatrix::MakeFromZ(EndLoc - StartLoc).ToQuat();

	DrawDebugCapsule(World, TraceCenter, TraceHalfHeight, InteractTraceRadius, TraceRotation, DrawColor, false, 2.0f);
#endif
}

void UInteract::OnTargetDataReadyCallback(const FGameplayAbilityTargetDataHandle& InData, FGameplayTag ApplicationTag)
{
	UAbilitySystemComponent* ASC = CurrentActorInfo->AbilitySystemComponent.Get();

	// 수신한 데이터를 소비하여 메모리 누수 방지
	ASC->ConsumeClientReplicatedTargetData(CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());

	ProcessInteract(InData);
}

void UInteract::ProcessInteract(const FGameplayAbilityTargetDataHandle& InData)
{
	// [서버]
	if (HasAuthority(&CurrentActivationInfo))
	{
		if (InData.Data.IsValidIndex(0))
		{
			const FHitResult* HitResult = InData.Data[0]->GetHitResult();
			AActor* Caster = Cast<AActor>(GetAvatarActorFromActorInfo());

			if (HitResult && HitResult->GetComponent() && Caster)
			{
				if (IInteractable* InteractableComp = Cast<IInteractable>(HitResult->GetComponent())) {
					// 상호작용 대상의 자체 로직 수행
					InteractableComp->Interact(Caster);

					// 상호작용 종류 식별 Tag
					FGameplayTag TargetTag = InteractableComp->GetInteractionTag();

					if (TargetTag.IsValid())
					{
						UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
						if (ASC)
						{
							// Payload 구성 및 이벤트 발송
							FGameplayEventData Payload;
							Payload.Instigator = Caster;
							Payload.Target = HitResult->GetActor(); // 상호작용한 액터를 Target에 담아 전달

							ASC->HandleGameplayEvent(TargetTag, &Payload);
						}
					}
				}				
			}
		}
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}