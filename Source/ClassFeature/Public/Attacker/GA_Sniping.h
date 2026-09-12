#pragma once

#include "CoreMinimal.h"
#include "GAS/Ability/PlayerCombatGameplayAbility.h"
#include "GA_Sniping.generated.h"

UCLASS()
class CLASSFEATURE_API UGA_Sniping : public UPlayerCombatGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Sniping();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Sniping")
	TSubclassOf<class UGameplayEffect> DamageEffectClass;

	UPROPERTY(EditDefaultsOnly, Category = "Sniping|Trace")
	float TraceDistance = 10000.0f;

	// 반동 상승 각도
	UPROPERTY(EditDefaultsOnly, Category = "Sniping|Recoil")
	float RecoilPitchOffset = 2.0f;

	// 반동 상승에 걸리는 시간 (프레임 단위 보간을 위한 시간)
	UPROPERTY(EditDefaultsOnly, Category = "Sniping|Recoil")
	float RecoilAscentTime = 0.05f;

	// 반동 회복에 걸리는 시간
	UPROPERTY(EditDefaultsOnly, Category = "Sniping|Recoil")
	float RecoilRecoveryTime = 0.3f;

	// 회복 후 제자리 오차 반경
	UPROPERTY(EditDefaultsOnly, Category = "Sniping|Recoil")
	float RecoilRestingRadius = 0.5f;

	UFUNCTION()
	void OnShoot(FGameplayEventData Payload);

	UFUNCTION()
	void OnRecoilFinished();

	UFUNCTION()
	void OnRightClick(float TimeWaited);

	bool bIsSnipingActive;
	bool bIsRecoiling;

	// Hitscan 처리를 서버로 전송하기 위한 함수 (GAS Target Data 시스템 사용)
	UFUNCTION()
	void OnTargetDataReceived(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag);
};
