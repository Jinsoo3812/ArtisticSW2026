// Fill out your copyright notice in the Description page of Project Settings.


#include "BasePlayerState.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"

ABasePlayerState::ABasePlayerState()
{
	// Ability System Component 생성
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);

	// Mixed: 
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
