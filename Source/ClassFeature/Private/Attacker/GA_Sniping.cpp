#include "Attacker/GA_Sniping.h"
#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"
#include "BasePlayer.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputPress.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Attacker/AbilityTask_RecoilInterpolation.h"
#include "BaseGameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraComponent.h"
#include "DrawDebugHelpers.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GAS/SWCombatEffectContextLibrary.h"

UGA_Sniping::UGA_Sniping()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	bIsSnipingActive = false;
	bIsRecoiling = false;
	
	// State_Sniping 태그를 부여하여 BasePlayer에서 카메라 줌인 등을 처리
	ActivationOwnedTags.AddTag(State_Sniping);
	FGameplayTagContainer Tags;
	Tags.AddTag(GameplayAbility_Weapon_AimCycle);
	Tags.AddTag(GameplayAbility_InterruptibleByHit);
	SetAssetTags(Tags);
}

void UGA_Sniping::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	LastServerShotTime = -DBL_MAX;
	bIsSnipingActive = true;
	bIsRecoiling = false;

	// 로컬 플레이어에게만 메시 숨기기 (1인칭 연출, 다른 플레이어에게는 보임)
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (Player && Player->IsLocallyControlled())
	{
		SavedOwnerNoSee = Player->GetMesh()->bOwnerNoSee;
		HiddenMesh = Player->GetMesh();
		Player->GetMesh()->SetOwnerNoSee(true);
	}

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

	// Target Data 수신 델리게이트 바인딩 (서버)
	if (HasAuthority(&ActivationInfo))
	{
		UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
		if (ASC)
		{
			TargetDataASC = ASC;
			TargetDataSubscription =
			    ASC->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
			        .AddUObject(this, &UGA_Sniping::OnTargetDataReceived);
			ASC->CallReplicatedTargetDataDelegatesIfSet(Handle, ActivationInfo.GetActivationPredictionKey());
		}
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
	if (!IsActive() || bIsRecoiling)
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

		// ECC_Pawn 채널을 사용하여 캐릭터 타겟 검출
		bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, StartLoc, EndLoc, ECC_Pawn, QueryParams);

		// 디버그 라인 그리기 (충돌 시 ImpactPoint까지, 아니면 끝까지)
		FVector TraceEnd = bHit ? HitResult.ImpactPoint : EndLoc;
		DrawDebugLine(GetWorld(), StartLoc, TraceEnd, FColor::Red, false, 3.0f, 0, 2.0f);

		if (bHit)
		{
			// 충돌 지점에 디버그 포인트 표시
			DrawDebugPoint(GetWorld(), HitResult.ImpactPoint, 10.0f, FColor::Green, false, 3.0f);
			UE_LOG(LogTemp, Log, TEXT("UGA_Sniping::OnShoot : Hit -> %s (Bone: %s)"), *HitResult.GetActor()->GetName(), *HitResult.BoneName.ToString());
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("UGA_Sniping::OnShoot : Missed (No hit)"));
		}

		if (bHit && HitResult.GetActor())
		{
			// Target Data 생성
			FGameplayAbilityTargetData_SingleTargetHit* HitData = new FGameplayAbilityTargetData_SingleTargetHit();
			HitData->HitResult = HitResult;

			FGameplayAbilityTargetDataHandle TargetDataHandle;
			TargetDataHandle.Add(HitData);

			// GAS Target Data 시스템을 통해 서버로 전송
			UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
			if (ASC)
			{
				FScopedPredictionWindow ScopedPrediction(ASC, true);
				ASC->CallServerSetReplicatedTargetData(
					GetCurrentAbilitySpecHandle(),
					GetCurrentActivationInfo().GetActivationPredictionKey(),
					TargetDataHandle,
					FGameplayTag(),
					ASC->ScopedPredictionKey
				);
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

bool UGA_Sniping::ValidateReportedHit(AActor* Source, const FHitResult& Reported, FHitResult& Verified) const
{
	if (!IsValid(Source) || !IsValid(Reported.GetActor()) || Source == Reported.GetActor() ||
	    Source->GetWorld() != Reported.GetActor()->GetWorld() || Reported.ImpactPoint.ContainsNaN() ||
	    !FMath::IsFinite(TraceDistance) || TraceDistance <= 0.f)
		return false;
	FVector Start;
	FRotator Rotation;
	Source->GetActorEyesViewPoint(Start, Rotation);
	const FVector To = Reported.ImpactPoint - Start;
	if (To.SizeSquared() > FMath::Square(TraceDistance) || To.IsNearlyZero())
		return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(SnipingServerValidation), true, Source);
	return Source->GetWorld()->LineTraceSingleByChannel(
	           Verified, Start, Reported.ImpactPoint + To.GetSafeNormal() * 20.f, ECC_Pawn, Query) &&
	       Verified.GetActor() == Reported.GetActor();
}

void UGA_Sniping::OnTargetDataReceived(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag)
{
	if (!IsActive() || !GetCurrentActorInfo() || !GetCurrentActorInfo()->IsNetAuthority())
		return;
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC || !GetWorld())
		return;
	const FGameplayAbilityTargetDataHandle Stable = Data;
	ASC->ConsumeClientReplicatedTargetData(GetCurrentAbilitySpecHandle(),
	                                       GetCurrentActivationInfo().GetActivationPredictionKey());
	const double Now = GetWorld()->GetTimeSeconds();
	const float Interval = FMath::Max(.01f, RecoilAscentTime + RecoilRecoveryTime);
	if (!DamageEffectClass || Stable.Num() != 1 || !Stable.Data[0].IsValid() || !FMath::IsFinite(Interval) ||
	    Now - LastServerShotTime < Interval)
		return;
	const FHitResult* Reported = Stable.Data[0]->GetHitResult();
	FHitResult Verified;
	if (!Reported || !ValidateReportedHit(GetAvatarActorFromActorInfo(), *Reported, Verified))
		return;
	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Verified.GetActor());
	if (!TargetASC)
		return;
	LastServerShotTime = Now;
	AActor* Source = GetAvatarActorFromActorInfo();
	FGameplayEffectContextHandle Context = USWCombatEffectContextLibrary::MakeCombatEffectContext(
	    ASC, Source, Source, Verified.GetActor(), true, Verified);
	USWCombatEffectContextLibrary::SetDamageDeliveryType(Context, ESWDamageDeliveryType::DirectHit);
	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(DamageEffectClass, 1.f, Context);
	if (Spec.IsValid())
		ASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
}

void UGA_Sniping::OnRecoilFinished()
{
	bIsRecoiling = false;
}

void UGA_Sniping::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	bIsSnipingActive = false;
	bIsRecoiling = false;

	if (USkeletalMeshComponent* Mesh = HiddenMesh.Get())
		Mesh->SetOwnerNoSee(SavedOwnerNoSee);
	HiddenMesh.Reset();
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
