// Fill out your copyright notice in the Description page of Project Settings.


#include "BaseGameplayAbility.h"
#include "AbilitySystemComponent.h"

UBaseGameplayAbility::UBaseGameplayAbility()
{
	// GA ?몄뒪?댁뒪 ?뺤콉
	// InstancedPerActor: ?≫꽣留덈떎 ?섎굹??GA ?몄뒪?댁뒪瑜??앹꽦 諛?愿由?
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// ?ㅽ듃?뚰겕 ?ㅽ뻾 ?뺤콉. 
	// LocalPredicted: ?대씪?댁뼵?몄뿉???낅젰 利됱떆 ?덉륫 ?ㅽ뻾 ???쒕쾭?먯꽌 寃利?
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UBaseGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	// GA ?쒖쟾 ????怨듯넻 濡쒖쭅
}

void UBaseGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	// GA 醫낅즺 ?꾩쓽 怨듯넻 濡쒖쭅

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

	// GE Spec Context ?앹꽦
	FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
	ContextHandle.AddInstigator(GetAvatarActorFromActorInfo(), GetAvatarActorFromActorInfo());

	// GE Spec ?앹꽦
	FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectClass, EffectLevel, ContextHandle);

	if (SpecHandle.IsValid())
	{
		// ?앹꽦??Spec??諛뷀깢?쇰줈 TargetData?먭쾶 ?쇨큵?곸쑝濡?GE瑜??곸슜?⑸땲??
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

	// GE Spec Context ?앹꽦
	FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
	ContextHandle.AddInstigator(GetAvatarActorFromActorInfo(), GetAvatarActorFromActorInfo());

	// GE Spec ?앹꽦
	FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectClass, EffectLevel, ContextHandle);

	if (SpecHandle.IsValid())
	{
		// ?먯떊(ASC)?먭쾶 吏곸젒 GE瑜??곸슜?⑸땲??
		return ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
	}

	return FActiveGameplayEffectHandle();
}
