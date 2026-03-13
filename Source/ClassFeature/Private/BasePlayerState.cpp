// Fill out your copyright notice in the Description page of Project Settings.


#include "BasePlayerState.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"

ABasePlayerState::ABasePlayerState()
{
	// 서버와 클라이언트 간의 데이터 갱신 주기 설정
	// 적응형 NetUpdateFrequency를 사용해 최적화 가능함
	NetUpdateFrequency = BaseNetUpdateFrequency;

	// Ability System Component 생성
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);

	// Mixed: 자신의 GE, Tags, Gameplay Cues 복제
	// 자신을 바라보는 타인은 자신의 GE까지 복제하지는 않음
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	// Basic Attribute Set 생성
	BasicAttributes = CreateDefaultSubobject<UBaseAttributeSet>(TEXT("BasicAttributeSet"));
}

UAbilitySystemComponent* ABasePlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

UBaseAttributeSet* ABasePlayerState::GetAttributeSet() const
{
	return BasicAttributes;
}
