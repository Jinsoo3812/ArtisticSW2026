#include "Attacker/AbilityTask_RecoilInterpolation.h"
#include "GameFramework/PlayerController.h"

UAbilityTask_RecoilInterpolation::UAbilityTask_RecoilInterpolation(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bTickingTask = true;
	ElapsedTime = 0.0f;
	bIsRecovering = false;
}

UAbilityTask_RecoilInterpolation* UAbilityTask_RecoilInterpolation::RecoilInterpolation(
	UGameplayAbility* OwningAbility, float InRecoilPitch, float InRecoilDuration, float InRecoveryDuration, float InRestingRadius)
{
	UAbilityTask_RecoilInterpolation* MyObj = NewAbilityTask<UAbilityTask_RecoilInterpolation>(OwningAbility);
	MyObj->RecoilPitch = InRecoilPitch;
	MyObj->RecoilDuration = InRecoilDuration;
	MyObj->RecoveryDuration = InRecoveryDuration;
	MyObj->RestingRadius = InRestingRadius;
	return MyObj;
}

void UAbilityTask_RecoilInterpolation::Activate()
{
	Super::Activate();

	if (APlayerController* PC = Cast<APlayerController>(Ability->GetActorInfo().PlayerController.Get()))
	{
		PlayerController = PC;
		OriginalRotation = PC->GetControlRotation();
		TargetRecoilRotation = OriginalRotation;
		TargetRecoilRotation.Pitch += RecoilPitch;

		// 랜덤 오프셋 계산 (원형)
		float RandomAngle = FMath::RandRange(0.0f, 360.0f);
		float RandomDist = FMath::RandRange(0.0f, RestingRadius);
		TargetRestingRotation = OriginalRotation;
		TargetRestingRotation.Yaw += FMath::Cos(FMath::DegreesToRadians(RandomAngle)) * RandomDist;
		TargetRestingRotation.Pitch += FMath::Sin(FMath::DegreesToRadians(RandomAngle)) * RandomDist;
	}
	else
	{
		EndTask();
	}
}

void UAbilityTask_RecoilInterpolation::TickTask(float DeltaTime)
{
	Super::TickTask(DeltaTime);

	if (!PlayerController.IsValid() || RecoilDuration <= 0.0f || RecoveryDuration <= 0.0f)
	{
		EndTask();
		return;
	}

	ElapsedTime += DeltaTime;

	if (!bIsRecovering)
	{
		// 반동 상승 단계
		if (ElapsedTime < RecoilDuration)
		{
			float Alpha = ElapsedTime / RecoilDuration;
			FRotator NewRotation = FMath::Lerp(OriginalRotation, TargetRecoilRotation, Alpha);
			PlayerController->SetControlRotation(NewRotation);
		}
		else
		{
			PlayerController->SetControlRotation(TargetRecoilRotation);
			bIsRecovering = true;
			ElapsedTime = 0.0f; // 리셋하여 회복 단계 시작
		}
	}
	else
	{
		// 반동 회복 단계
		if (ElapsedTime < RecoveryDuration)
		{
			float Alpha = ElapsedTime / RecoveryDuration;
			FRotator NewRotation = FMath::Lerp(TargetRecoilRotation, TargetRestingRotation, Alpha);
			PlayerController->SetControlRotation(NewRotation);
		}
		else
		{
			PlayerController->SetControlRotation(TargetRestingRotation);
			if (ShouldBroadcastAbilityTaskDelegates())
			{
				OnRecoilFinished.Broadcast();
			}
			EndTask();
		}
	}
}
