// Fill out your copyright notice in the Description page of Project Settings.

#include "Interact.h"
#include "Interactable.h"
#include "BaseGameplayTags.h"
#include "CollisionChannels.h"
#include "Camera/CameraComponent.h"
#include "AbilitySystemComponent.h"
#include "DrawDebugHelpers.h"
#include "BasePlayer.h"

void UInteract::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	bool bIsLocallyControlled = ActorInfo->IsLocallyControlled();
	bool bHasAuthority = HasAuthority(&ActivationInfo);

	UE_LOG(LogTemp, Log, TEXT("UInteract::ActivateAbility - [%s] Started. LocallyControlled: %s"),
		bHasAuthority ? TEXT("SERVER") : TEXT("CLIENT"),
		bIsLocallyControlled ? TEXT("YES") : TEXT("NO"));

	FGameplayAbilityTargetDataHandle TargetDataHandle;

	// [클라] 로컬에서 Trace 수행 후 서버로 TargetData 전송
	if (bIsLocallyControlled)
	{
		TArray<FHitResult> HitResults;
		ABasePlayer* PlayerAvatar = Cast<ABasePlayer>(ActorInfo->AvatarActor.Get());

		UE_LOG(LogTemp, Log, TEXT("UInteract::ActivateAbility - [CLIENT] Performing interact trace on player character: %s"),
			PlayerAvatar ? *PlayerAvatar->GetName() : TEXT("None"));

		if (PlayerAvatar && PlayerAvatar->PerformInteractTrace(HitResults) && HitResults.Num() > 0)
		{
			FVector StartLoc = PlayerAvatar->GetActorLocation();
			float ClosestDistanceSq = MAX_flt;
			FHitResult BestHit;
			bool bFoundValidHit = false;

			// 반환된 모든 히트 결과를 순회하며 최단 거리 객체 판별
			for (int32 Index = 0; Index < HitResults.Num(); ++Index)
			{
				const FHitResult& Hit = HitResults[Index];
				UE_LOG(LogTemp, Log, TEXT("UInteract::ActivateAbility - [CLIENT] Trace Hit [%d]: Actor: %s, Component: %s, ImpactPoint: %s"),
					Index,
					Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("None"),
					Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("None"),
					*Hit.ImpactPoint.ToString());
				
				float DistSq = FVector::DistSquared(StartLoc, Hit.ImpactPoint);
				if (DistSq < ClosestDistanceSq)
				{
					ClosestDistanceSq = DistSq;
					BestHit = Hit;
					bFoundValidHit = true;
				}
			}

			// 가장 가까운 객체 하나만 TargetData로 패키징
			if (bFoundValidHit)
			{
				UE_LOG(LogTemp, Log, TEXT("UInteract::ActivateAbility - [CLIENT] Best Hit chosen: Actor: %s, Component: %s, DistanceSq: %f"),
					BestHit.GetActor() ? *BestHit.GetActor()->GetName() : TEXT("None"),
					BestHit.GetComponent() ? *BestHit.GetComponent()->GetName() : TEXT("None"),
					ClosestDistanceSq);

				FGameplayAbilityTargetData_SingleTargetHit* TargetData = new FGameplayAbilityTargetData_SingleTargetHit();
				TargetData->HitResult = BestHit;
				TargetDataHandle.Add(TargetData);
			}
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("UInteract::ActivateAbility - [CLIENT] PerformInteractTrace returned false or no hits. Avatar valid: %s, HitCount: %d"),
				PlayerAvatar ? TEXT("YES") : TEXT("NO"),
				HitResults.Num());
		}

		// 서버로 TargetData 전송
		UE_LOG(LogTemp, Log, TEXT("UInteract::ActivateAbility - [CLIENT] Calling CallServerSetReplicatedTargetData. NumTargetData: %d"),
			TargetDataHandle.Num());

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
			UE_LOG(LogTemp, Log, TEXT("UInteract::ActivateAbility - [SERVER] Standalone mode: directly processing interaction."));
			ProcessInteract(TargetDataHandle);
		}
		else
		{
			// 실제 환경 (Dedicated) 에서는 클라이언트가 보낸 데이터를 기다려야 하므로 델리게이트 바인딩
			UE_LOG(LogTemp, Log, TEXT("UInteract::ActivateAbility - [SERVER] Dedicated mode: binding AbilityTargetDataSetDelegate. PredictionKey: %d"),
				ActivationInfo.GetActivationPredictionKey().Current);

			ASC->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey()).AddUObject(this, &UInteract::OnTargetDataReadyCallback);

			// 이미 도착한 데이터가 있다면 델리게이트를 즉시 실행
			ASC->CallReplicatedTargetDataDelegatesIfSet(Handle, ActivationInfo.GetActivationPredictionKey());
		}
	}

	// [클라] 로컬은 데이터를 보냈으므로 예측 단계에서 어빌리티 종료 
	// [Antigravity] 멀티플레이어 상호작용 버그 수정: 클라이언트가 데이터를 보낸 직후 어빌리티를 직접 종료해버리면,
	// 서버가 네트워크를 통해 TargetData를 전달받기도 전에 어빌리티가 종료되어 대포/선박 승선 상호작용이 무시되는 현상이 생깁니다.
	// 따라서 서버가 TargetData를 수신하여 최종적으로 EndAbility를 호출하고 이를 복제(Replicate)할 때까지 대기하도록 변경합니다.
	/*if (bIsLocallyControlled && !bHasAuthority)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	}*/
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
#if WITH_EDITOR
	if (GIsEditor)
	{
		FColor DrawColor = OutHitResult.bBlockingHit ? FColor::Green : FColor::Red;
		FVector TraceCenter = StartLoc + (EndLoc - StartLoc) * 0.5f;
		float TraceHalfHeight = (EndLoc - StartLoc).Size() * 0.5f;
		FQuat TraceRotation = FRotationMatrix::MakeFromZ(EndLoc - StartLoc).ToQuat();

		// DrawDebugCapsule(World, TraceCenter, TraceHalfHeight, InteractTraceRadius, TraceRotation, DrawColor, false, 2.0f);
	}
#endif
#endif
}

void UInteract::OnTargetDataReadyCallback(const FGameplayAbilityTargetDataHandle& InData, FGameplayTag ApplicationTag)
{
	UAbilitySystemComponent* ASC = CurrentActorInfo->AbilitySystemComponent.Get();

	UE_LOG(LogTemp, Log, TEXT("UInteract::OnTargetDataReadyCallback - [SERVER] Received replicated target data from client. NumData: %d"),
		InData.Num());

	// 수신한 데이터를 소비하여 메모리 누수 방지
	ASC->ConsumeClientReplicatedTargetData(CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());

	ProcessInteract(InData);
}

void UInteract::ProcessInteract(const FGameplayAbilityTargetDataHandle& InData)
{
	bool bHasAuth = HasAuthority(&CurrentActivationInfo);
	UE_LOG(LogTemp, Log, TEXT("UInteract::ProcessInteract - [%s] Processing. NumData: %d"),
		bHasAuth ? TEXT("SERVER") : TEXT("CLIENT"),
		InData.Num());

	// [서버]
	if (bHasAuth)
	{
		if (InData.Data.IsValidIndex(0))
		{
			const FHitResult* HitResult = InData.Data[0]->GetHitResult();
			AActor* Caster = Cast<AActor>(GetAvatarActorFromActorInfo());

			UE_LOG(LogTemp, Log, TEXT("UInteract::ProcessInteract - [SERVER] HitResult valid: %s, Caster valid: %s"),
				HitResult ? TEXT("YES") : TEXT("NO"),
				Caster ? TEXT("YES") : TEXT("NO"));

			if (HitResult && HitResult->GetComponent() && Caster)
			{
				UE_LOG(LogTemp, Log, TEXT("UInteract::ProcessInteract - [SERVER] Hit Actor: %s, Hit Component: %s"),
					HitResult->GetActor() ? *HitResult->GetActor()->GetName() : TEXT("None"),
					*HitResult->GetComponent()->GetName());

				if (IInteractable* InteractableComp = Cast<IInteractable>(HitResult->GetComponent())) {
					// 상호작용 대상의 자체 로직 수행
					UE_LOG(LogTemp, Log, TEXT("UInteract::ProcessInteract - [SERVER] Invoking Interact on component."));
					InteractableComp->Interact(Caster);

					// 상호작용 종류 식별 Tag
					FGameplayTag TargetTag = InteractableComp->GetInteractionTag();
					UE_LOG(LogTemp, Log, TEXT("UInteract::ProcessInteract - [SERVER] Component InteractionTag: %s"),
						*TargetTag.ToString());

					if (TargetTag.IsValid())
					{
						UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
						if (ASC)
						{
							// Payload 구성 및 이벤트 발송
							FGameplayEventData Payload;
							Payload.Instigator = Caster;
							Payload.Target = HitResult->GetActor(); // 상호작용한 액터를 Target에 담아 전달

							UE_LOG(LogTemp, Log, TEXT("UInteract::ProcessInteract - [SERVER] Firing HandleGameplayEvent with tag: %s"),
								*TargetTag.ToString());

							ASC->HandleGameplayEvent(TargetTag, &Payload);
						}
						else
						{
							UE_LOG(LogTemp, Warning, TEXT("UInteract::ProcessInteract - [SERVER] ASC is null on player avatar!"));
						}
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("UInteract::ProcessInteract - [SERVER] TargetTag is invalid/empty."));
					}
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("UInteract::ProcessInteract - [SERVER] Hit Component does not implement IInteractable! Component: %s"),
						*HitResult->GetComponent()->GetName());
				}
			}
			else if (HitResult)
			{
				UE_LOG(LogTemp, Warning, TEXT("UInteract::ProcessInteract - [SERVER] Component or Caster is null. Component: %s, Caster: %s"),
					HitResult->GetComponent() ? TEXT("Valid") : TEXT("Null"),
					Caster ? TEXT("Valid") : TEXT("Null"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("UInteract::ProcessInteract - [SERVER] InData.Data does not have valid index 0."));
		}
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}