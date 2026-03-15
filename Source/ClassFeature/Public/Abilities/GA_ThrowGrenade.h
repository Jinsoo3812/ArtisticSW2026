#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_ThrowGrenade.generated.h"

UCLASS()
class CLASSFEATURE_API UGA_ThrowGrenade : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_ThrowGrenade();

	// 스킬이 활성화될 때 실행 (Q를 누른 시점)
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

protected:
	// Q를 뗐을 때 실행될 콜백
	UFUNCTION()
	void OnInputReleased(float TimeHeld);

	// 발사 속도 및 방향 계산
	FVector CalculateLaunchVelocity(class ACharacter* AvatarChar);

	// ==========================================
	// 설정 변수 (블루프린트에서 지정)
	// ==========================================
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grenade")
	TSubclassOf<class AGrenadeProjectile> GrenadeClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grenade")
	TSubclassOf<class UGameplayEffect> DamageEffectClass;

	UPROPERTY(EditDefaultsOnly, Category = "Grenade")
	float ThrowForce;
};