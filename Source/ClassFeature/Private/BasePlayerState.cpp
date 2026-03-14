// Fill out your copyright notice in the Description page of Project Settings.


#include "BasePlayerState.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"

ABasePlayerState::ABasePlayerState()
{
	// ?쒕쾭? ?대씪?댁뼵??媛꾩쓽 ?곗씠??媛깆떊 二쇨린 ?ㅼ젙
	// ?곸쓳??NetUpdateFrequency瑜??ъ슜??理쒖쟻??媛?ν븿
	NetUpdateFrequency = BaseNetUpdateFrequency;

	// Ability System Component ?앹꽦
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);

	// Mixed: ?먯떊??GE, Tags, Gameplay Cues 蹂듭젣
	// ?먯떊??諛붾씪蹂대뒗 ??몄? ?먯떊??GE源뚯? 蹂듭젣?섏????딆쓬
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	// Basic Attribute Set ?앹꽦
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
