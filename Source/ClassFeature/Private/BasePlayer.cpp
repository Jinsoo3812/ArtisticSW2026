// Fill out your copyright notice in the Description page of Project Settings.


#include "BasePlayer.h"
#include "BasePlayerState.h"
#include "AbilitySystemComponent.h"

ABasePlayer::ABasePlayer()
{
	// 移대찓?쇰굹 ?ㅽ봽留곸븫 媛숈? ?뚮젅?댁뼱 ?꾩슜 而댄룷?뚰듃 珥덇린??
}

void ABasePlayer::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// ?쒕쾭 痢?ASC 珥덇린??(InitAbilityActorInfo)
	ABasePlayerState* PS = GetPlayerState<ABasePlayerState>();
	if (PS)
	{
		// Owner??PlayerState, Avatar????Character 媛앹껜濡??ㅼ젙
		PS->GetAbilitySystemComponent()->InitAbilityActorInfo(PS, this);

		// PlayerState濡?遺??ASC ?ъ씤??媛?몄???罹먯떛
		AbilitySystemComponent = PS->GetAbilitySystemComponent();

		// 遺紐??대옒?ㅼ뿉 援ы쁽???대퉴由ы떚 遺???⑥닔 ?몄텧 (?쒕쾭?먯꽌留?
		GrantAbilities(StartingAbilities);
	}
}

void ABasePlayer::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// ?대씪?댁뼵??痢?ASC 珥덇린??(PlayerState媛 ?대씪濡?蹂듭젣?섏뿀?뚯쓣 蹂댁옣?섎뒗 ??대컢)
	ABasePlayerState* PS = GetPlayerState<ABasePlayerState>();
	if (PS)
	{
		// ?대씪?댁뼵?몄뿉?쒕룄 Owner? Avatar瑜??곌껐?댁쨲
		PS->GetAbilitySystemComponent()->InitAbilityActorInfo(PS, this);

		// ?대씪?댁뼵??痢??ъ씤??媛깆떊
		AbilitySystemComponent = PS->GetAbilitySystemComponent();
	}
}

void ABasePlayer::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// 李⑦썑 Enhanced Input怨?ASC??BindAbilityActivationToInputComponent瑜??곌껐??遺遺?
}
