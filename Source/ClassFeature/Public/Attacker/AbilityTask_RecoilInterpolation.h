#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "AbilityTask_RecoilInterpolation.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRecoilTaskDelegate);

/**
 * 사격 반동(Pitch 상승 및 원래 위치로의 회복 보간)을 처리하는 Ability Task
 */
UCLASS()
class CLASSFEATURE_API UAbilityTask_RecoilInterpolation : public UAbilityTask
{
	GENERATED_BODY()

public:
	UAbilityTask_RecoilInterpolation(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
	static UAbilityTask_RecoilInterpolation* RecoilInterpolation(
		UGameplayAbility* OwningAbility, 
		float InRecoilPitch, 
		float InRecoilDuration, 
		float InRecoveryDuration, 
		float InRestingRadius
	);

	virtual void Activate() override;
	virtual void TickTask(float DeltaTime) override;

	UPROPERTY(BlueprintAssignable)
	FRecoilTaskDelegate OnRecoilFinished;

protected:
	float RecoilPitch;
	float RecoilDuration;
	float RecoveryDuration;
	float RestingRadius;

	float ElapsedTime;
	bool bIsRecovering;

	FRotator OriginalRotation;
	FRotator TargetRecoilRotation;
	FRotator TargetRestingRotation;

	TWeakObjectPtr<class APlayerController> PlayerController;
};
