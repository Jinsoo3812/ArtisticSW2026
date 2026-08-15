// Fill out your copyright notice in the Description page of Project Settings.


#include "BaseGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"

UBaseGameplayAbility::UBaseGameplayAbility()
{
	// 액터마다 하나의 Ability 인스턴스를 유지합니다.
	// 콤보 카운트, 차징 상태처럼 Ability 내부 상태를 저장하기 좋습니다.
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// 로컬 클라이언트에서 먼저 예측 실행하고 서버가 검증합니다.
	// 입력 반응성이 중요한 플레이어 Ability의 기본값으로 적합합니다.
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	// Dialogue owns player input until the session ends. All project abilities
	// derive from this base, so one shared rule prevents combat/interaction races.
	ActivationBlockedTags.AddTag(State_Dialogue);
}

void UBaseGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	// 파생 Ability에서 Super 호출 뒤 공통 시작 로직을 이어서 확장할 수 있습니다.
}

void UBaseGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	// 파생 Ability에서 Super 호출 전 필요한 정리 로직을 먼저 실행할 수 있습니다.

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

	// Ability 소유자를 Instigator로 기록한 GE Context를 만듭니다.
	FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
	ContextHandle.AddInstigator(GetAvatarActorFromActorInfo(), GetAvatarActorFromActorInfo());

	// 적용할 GameplayEffect Spec을 생성합니다.
	FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectClass, EffectLevel, ContextHandle);

	if (SpecHandle.IsValid())
	{
		// TargetData에 들어 있는 대상 ASC에 Spec을 일괄 적용합니다.
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

	// Ability 소유자를 Instigator로 기록한 GE Context를 만듭니다.
	FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
	ContextHandle.AddInstigator(GetAvatarActorFromActorInfo(), GetAvatarActorFromActorInfo());

	// 적용할 GameplayEffect Spec을 생성합니다.
	FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectClass, EffectLevel, ContextHandle);

	if (SpecHandle.IsValid())
	{
		// 자신이 가진 ASC에 Spec을 직접 적용합니다.
		return ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
	}

	return FActiveGameplayEffectHandle();
}
