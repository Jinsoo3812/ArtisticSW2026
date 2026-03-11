// Fill out your copyright notice in the Description page of Project Settings.


#include "BaseGameplayAbility.h"
#include "AbilitySystemComponent.h"

UBaseGameplayAbility::UBaseGameplayAbility()
{
	// GA 인스턴스 정책
	// InstancedPerActor: 액터마다 하나의 GA 인스턴스를 생성 및 관리
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// 네트워크 실행 정책. 
	// LocalPredicted: 클라이언트에서 입력 즉시 예측 실행 후 서버에서 검증
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UBaseGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	// GA 시전 후 의 공통 로직
}

void UBaseGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	// GA 종료 전의 공통 로직

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

TArray<FActiveGameplayEffectHandle> UBaseGameplayAbility::ApplyEffectToTargetData(
	const FGameplayAbilityTargetDataHandle& TargetData,
	TSubclassOf<class UGameplayEffect> EffectClass, int32 EffectLevel)
{
	TArray<FActiveGameplayEffectHandle> AppliedEffects;

	if (!EffectClass || TargetData.Data.Num() == 0)
	{
		return AppliedEffects;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return AppliedEffects;
	}

	// GE Spec Context 생성
	FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
	ContextHandle.AddInstigator(GetAvatarActorFromActorInfo(), GetAvatarActorFromActorInfo());

	// GE Spec 생성
	FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectClass, EffectLevel, ContextHandle);

	if (SpecHandle.IsValid())
	{
		// 생성된 Spec을 바탕으로 TargetData에게 일괄적으로 GE를 적용합니다.
		AppliedEffects = K2_ApplyGameplayEffectSpecToTarget(SpecHandle, TargetData);
	}

	return AppliedEffects;
}

FActiveGameplayEffectHandle UBaseGameplayAbility::ApplyEffectToOwner(TSubclassOf<class UGameplayEffect> EffectClass, int32 EffectLevel)
{
	if (!EffectClass)
	{
		return FActiveGameplayEffectHandle();
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return FActiveGameplayEffectHandle();
	}

	// GE Spec Context 생성
	FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
	ContextHandle.AddInstigator(GetAvatarActorFromActorInfo(), GetAvatarActorFromActorInfo());

	// GE Spec 생성
	FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectClass, EffectLevel, ContextHandle);

	if (SpecHandle.IsValid())
	{
		// 자신(ASC)에게 직접 GE를 적용합니다.
		return ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
	}

	return FActiveGameplayEffectHandle();
}
