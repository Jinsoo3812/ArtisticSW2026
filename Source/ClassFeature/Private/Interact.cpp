// Fill out your copyright notice in the Description page of Project Settings.

#include "Interact.h"
#include "BasePlayer.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Interactable.h"
#include "BaseGameplayTags.h"
#include "CollisionChannels.h"
#include "Camera/CameraComponent.h"
#include "AbilitySystemComponent.h"
#include "DrawDebugHelpers.h"
#include "BasePlayer.h"
#include "Storage/StorageInteractionDiagnostics.h"

void UInteract::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!ActorInfo)
		return;
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC || !ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	bool bIsLocallyControlled = ActorInfo->IsLocallyControlled();
	bool bHasAuthority = HasAuthority(&ActivationInfo);
	const bool bLogInteraction = IsStorageInteractionLoggingEnabled();
	if (bLogInteraction)
	{
		UE_LOG(LogStorageInteraction, Warning,
			TEXT("[Ability] Activated. Avatar=%s Local=%d Authority=%d ASC=%s"),
			*GetNameSafe(ActorInfo->AvatarActor.Get()), bIsLocallyControlled, bHasAuthority, *GetNameSafe(ASC));
	}

	// UE_LOG(LogTemp, Log, TEXT("UInteract::ActivateAbility - [%s] Started. LocallyControlled: %s"),
	// 	bHasAuthority ? TEXT("SERVER") : TEXT("CLIENT"),
	// 	bIsLocallyControlled ? TEXT("YES") : TEXT("NO"));

	FGameplayAbilityTargetDataHandle TargetDataHandle;

	// [클라] 로컬에서 Trace 수행 후 서버로 TargetData 전송
	if (bIsLocallyControlled)
	{
		TArray<FHitResult> HitResults;
		ABasePlayer* PlayerAvatar = Cast<ABasePlayer>(ActorInfo->AvatarActor.Get());

		// UE_LOG(LogTemp, Log, TEXT("UInteract::ActivateAbility - [CLIENT] Performing interact trace on player character: %s"),
		// 	PlayerAvatar ? *PlayerAvatar->GetName() : TEXT("None"));

		const bool bTraceHit = PlayerAvatar && PlayerAvatar->PerformInteractTrace(HitResults);
		if (bLogInteraction)
		{
			UE_LOG(LogStorageInteraction, Warning,
				TEXT("[Trace] Player=%s SweepHit=%d HitCount=%d"),
				*GetNameSafe(PlayerAvatar), bTraceHit, HitResults.Num());
		}
		if (bTraceHit && HitResults.Num() > 0)
		{
			FVector StartLoc = PlayerAvatar->GetActorLocation();
			float ClosestDistanceSq = MAX_flt;
			FHitResult BestHit;
			bool bFoundValidHit = false;

			// Only real interaction components may receive target data. For initial
			// overlaps, ImpactPoint is a penetration contact, not a useful measure
			// of which nearby actor the player intended to interact with.
			for (int32 Index = 0; Index < HitResults.Num(); ++Index)
			{
				const FHitResult& Hit = HitResults[Index];
				const bool bInteractable = Cast<IInteractable>(Hit.GetComponent()) != nullptr;
				const float SelectionDistanceSq = bInteractable && Hit.GetActor()
					? FVector::DistSquared(StartLoc,
						Hit.bStartPenetrating ? Hit.GetActor()->GetActorLocation() : FVector(Hit.ImpactPoint))
					: MAX_flt;
				if (bLogInteraction)
				{
					UE_LOG(LogStorageInteraction, Warning,
						TEXT("[Trace] Hit[%d] Actor=%s Component=%s Interactable=%d Blocking=%d InitialOverlap=%d Distance=%.1f Impact=%s Location=%s SelectionDistanceSq=%.1f"),
						Index, *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()),
						bInteractable, Hit.bBlockingHit,
						Hit.bStartPenetrating, Hit.Distance,
						*Hit.ImpactPoint.ToCompactString(), *Hit.Location.ToCompactString(), SelectionDistanceSq);
				}
				// UE_LOG(LogTemp, Log, TEXT("UInteract::ActivateAbility - [CLIENT] Trace Hit [%d]: Actor: %s, Component: %s, ImpactPoint: %s"),
				// 	Index,
				// 	Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("None"),
				// 	Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("None"),
				// 	*Hit.ImpactPoint.ToString());

				if (SelectionDistanceSq < ClosestDistanceSq)
				{
					ClosestDistanceSq = SelectionDistanceSq;
					BestHit = Hit;
					bFoundValidHit = true;
				}
			}

			// 가장 가까운 객체 하나만 TargetData로 패키징
			if (bFoundValidHit)
			{
				if (bLogInteraction)
				{
					UE_LOG(LogStorageInteraction, Warning,
						TEXT("[Trace] Selected Actor=%s Component=%s Interactable=%d SelectionDistanceSq=%.1f"),
						*GetNameSafe(BestHit.GetActor()), *GetNameSafe(BestHit.GetComponent()),
						Cast<IInteractable>(BestHit.GetComponent()) != nullptr, ClosestDistanceSq);
				}
				// UE_LOG(LogTemp, Log, TEXT("UInteract::ActivateAbility - [CLIENT] Best Hit chosen: Actor: %s, Component: %s, DistanceSq: %f"),
				// 	BestHit.GetActor() ? *BestHit.GetActor()->GetName() : TEXT("None"),
				// 	BestHit.GetComponent() ? *BestHit.GetComponent()->GetName() : TEXT("None"),
				// 	ClosestDistanceSq);

				FGameplayAbilityTargetData_SingleTargetHit* TargetData = new FGameplayAbilityTargetData_SingleTargetHit();
				TargetData->HitResult = BestHit;
				TargetDataHandle.Add(TargetData);
			}
			else if (bLogInteraction)
			{
				UE_LOG(LogStorageInteraction, Warning, TEXT("[Trace] No interactable component among sweep hits."));
			}
		}
		else
		{
			if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[Trace] No target data: player missing or sweep returned no hits."));
			// UE_LOG(LogTemp, Log, TEXT("UInteract::ActivateAbility - [CLIENT] PerformInteractTrace returned false or no hits. Avatar valid: %s, HitCount: %d"),
			// 	PlayerAvatar ? TEXT("YES") : TEXT("NO"),
			// 	HitResults.Num());
		}

		// 서버로 TargetData 전송
		// UE_LOG(LogTemp, Log, TEXT("UInteract::ActivateAbility - [CLIENT] Calling CallServerSetReplicatedTargetData. NumTargetData: %d"),
		// 	TargetDataHandle.Num());

		if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[Network] Sending target data. Count=%d"), TargetDataHandle.Num());
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
			if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[Network] Standalone/listen authority: process local target data."));
			// UE_LOG(LogTemp, Log, TEXT("UInteract::ActivateAbility - [SERVER] Standalone mode: directly processing interaction."));
			ProcessInteract(TargetDataHandle);
		}
		else
		{
			if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[Network] Authority waiting for client target data."));
			// 실제 환경 (Dedicated) 에서는 클라이언트가 보낸 데이터를 기다려야 하므로 델리게이트 바인딩
			// UE_LOG(LogTemp, Log, TEXT("UInteract::ActivateAbility - [SERVER] Dedicated mode: binding AbilityTargetDataSetDelegate. PredictionKey: %d"),
			// 	ActivationInfo.GetActivationPredictionKey().Current);

			TargetDataASC = ASC;
			TargetDataSubscription =
			    ASC->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
			        .AddUObject(this, &UInteract::OnTargetDataReadyCallback);
			GetWorld()->GetTimerManager().SetTimer(TargetDataTimeout, this, &UInteract::OnTargetDataTimeout,
			                                       FMath::Max(.1f, TargetDataTimeoutSeconds), false);

			// 이미 도착한 데이터가 있다면 델리게이트를 즉시 실행
			ASC->CallReplicatedTargetDataDelegatesIfSet(Handle, ActivationInfo.GetActivationPredictionKey());
		}
	}

	// [클라] 로컬은 데이터를 보냈으므로 예측 단계에서 어빌리티 종료
	// [Antigravity] 멀티플레이어 상호작용 버그 수정: 클라이언트가 데이터를 보낸 직후 어빌리티를 직접 종료해버리면,
	// 서버가 네트워크를 통해 TargetData를 전달받기도 전에 어빌리티가 종료되어 대포/선박 승선 상호작용이 무시되는 현상이
	// 생깁니다. 따라서 서버가 TargetData를 수신하여 최종적으로 EndAbility를 호출하고 이를 복제(Replicate)할 때까지
	// 대기하도록 변경합니다.
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
	if (IsStorageInteractionLoggingEnabled())
	{
		UE_LOG(LogStorageInteraction, Warning, TEXT("[Network] Server received client target data. Count=%d"), InData.Num());
	}
	if (!IsActive() || !CurrentActorInfo)
		return;
	UAbilitySystemComponent* ASC = CurrentActorInfo->AbilitySystemComponent.Get();
	if (!ASC)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}
	const FGameplayAbilityTargetDataHandle StableData =
	    CopyAndConsumeTargetData(ASC, CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey(), InData);
	ProcessInteract(StableData);
}

void UInteract::ProcessInteract(const FGameplayAbilityTargetDataHandle& InData)
{
	bool bHasAuth = HasAuthority(&CurrentActivationInfo);
	const bool bLogInteraction = IsStorageInteractionLoggingEnabled();
	if (bLogInteraction)
	{
		UE_LOG(LogStorageInteraction, Warning, TEXT("[Server] Process target data. Authority=%d Count=%d"), bHasAuth, InData.Num());
	}
	UE_LOG(LogTemp, Log, TEXT("UInteract::ProcessInteract - [%s] Processing. NumData: %d"),
		bHasAuth ? TEXT("SERVER") : TEXT("CLIENT"),
		InData.Num());

	// [서버]
	if (bHasAuth)
	{
		if (InData.Data.IsValidIndex(0) && InData.Data[0].IsValid())
		{
			const FHitResult* HitResult = InData.Data[0]->GetHitResult();
			AActor* Caster = Cast<AActor>(GetAvatarActorFromActorInfo());

			UE_LOG(LogTemp, Log, TEXT("UInteract::ProcessInteract - [SERVER] HitResult valid: %s, Caster valid: %s"),
				HitResult ? TEXT("YES") : TEXT("NO"),
				Caster ? TEXT("YES") : TEXT("NO"));

			if (HitResult && ValidateServerTarget(Caster, HitResult->GetComponent()))
			{
				if (bLogInteraction)
				{
					UE_LOG(LogStorageInteraction, Warning,
						TEXT("[Server] Target Actor=%s Component=%s Caster=%s Interactable=%d"),
						*GetNameSafe(HitResult->GetActor()), *GetNameSafe(HitResult->GetComponent()),
						*GetNameSafe(Caster), Cast<IInteractable>(HitResult->GetComponent()) != nullptr);
				}
				UE_LOG(LogTemp, Log, TEXT("UInteract::ProcessInteract - [SERVER] Hit Actor: %s, Hit Component: %s"),
					HitResult->GetActor() ? *HitResult->GetActor()->GetName() : TEXT("None"),
					*HitResult->GetComponent()->GetName());

				if (IInteractable* InteractableComp = Cast<IInteractable>(HitResult->GetComponent())) {
					if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[Server] Calling Interact on %s."), *GetNameSafe(HitResult->GetComponent()));
					// 상호작용 대상의 자체 로직 수행
					UE_LOG(LogTemp, Log, TEXT("UInteract::ProcessInteract - [SERVER] Invoking Interact on component."));
					const FGameplayTag TargetTag = InteractableComp->GetInteractionTag();
					TWeakObjectPtr<AActor> Target = HitResult->GetComponent()->GetOwner();
					InteractableComp->Interact(Caster);
					if (!Target.IsValid() || !IsValid(Caster))
					{
						if (IsActive())
							EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
						return;
					}
					if (bLogInteraction)
						UE_LOG(LogStorageInteraction, Warning, TEXT("[Server] Interact returned. Tag=%s"),
						       *TargetTag.ToString());

					// 상호작용 종류 식별 Tag

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
							Payload.Target = Target.Get(); // 상호작용한 액터를 Target에 담아 전달

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
					if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[Server] Rejected: selected component does not implement IInteractable."));
					UE_LOG(LogTemp, Warning, TEXT("UInteract::ProcessInteract - [SERVER] Hit Component does not implement IInteractable! Component: %s"),
						*HitResult->GetComponent()->GetName());
				}
			}
			else if (HitResult)
			{
				if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[Server] Rejected: missing hit component or caster."));
				UE_LOG(LogTemp, Warning, TEXT("UInteract::ProcessInteract - [SERVER] Component or Caster is null. Component: %s, Caster: %s"),
					HitResult->GetComponent() ? TEXT("Valid") : TEXT("Null"),
					Caster ? TEXT("Valid") : TEXT("Null"));
			}
			else if (bLogInteraction)
			{
				UE_LOG(LogStorageInteraction, Warning, TEXT("[Server] Rejected: target data contains no hit result."));
			}
		}
		else
		{
			if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[Server] Rejected: target data is empty."));
			UE_LOG(LogTemp, Warning, TEXT("UInteract::ProcessInteract - [SERVER] InData.Data does not have valid index 0."));
		}
	}

	if (IsActive())
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

FGameplayAbilityTargetDataHandle UInteract::CopyAndConsumeTargetData(UAbilitySystemComponent* ASC,
                                                                     FGameplayAbilitySpecHandle Handle,
                                                                     FPredictionKey Key,
                                                                     const FGameplayAbilityTargetDataHandle& Data)
{
	FGameplayAbilityTargetDataHandle Stable = Data; // Consume may reset the borrowed event parameter.
	if (ASC)
		ASC->ConsumeClientReplicatedTargetData(Handle, Key);
	return Stable;
}

bool UInteract::ValidateServerTarget(AActor* Caster, UPrimitiveComponent* Component) const
{
	if (!IsValid(Caster) || !IsValid(Component) || Caster->GetWorld() != Component->GetWorld() ||
	    !IsValid(Component->GetOwner()) || Component->GetOwner()->IsHidden() ||
	    !Component->GetOwner()->GetActorEnableCollision() || !Component->IsCollisionEnabled())
		return false;
	if (const ABasePlayer* Player = Cast<ABasePlayer>(Caster); Player && !Player->CanMutateLifeGameplay())
		return false;
	const float Reach = FMath::Max(0.f, InteractTraceDistance) + FMath::Max(0.f, InteractTraceRadius) +
	                    FMath::Max(0.f, ServerReachTolerance);
	const FVector Origin = Caster->GetActorLocation();
	return FMath::IsFinite(Reach) && !Origin.ContainsNaN() &&
	       Component->Bounds.GetBox().ComputeSquaredDistanceToPoint(Origin) <= FMath::Square(Reach);
}

void UInteract::OnTargetDataTimeout()
{
	if (IsActive())
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UInteract::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                           const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility,
                           bool bWasCancelled)
{
	if (GetWorld())
		GetWorld()->GetTimerManager().ClearTimer(TargetDataTimeout);
	if (UAbilitySystemComponent* ASC = TargetDataASC.Get())
	{
		ASC->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
		    .Remove(TargetDataSubscription);
		ASC->ConsumeClientReplicatedTargetData(Handle, ActivationInfo.GetActivationPredictionKey());
	}
	TargetDataSubscription.Reset();
	TargetDataASC.Reset();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
