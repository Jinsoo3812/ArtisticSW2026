// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseCharacter.h"
#include "BasePlayer.generated.h"

/**
 * 
 */
UCLASS()
class CLASSFEATURE_API ABasePlayer : public ABaseCharacter
{
	GENERATED_BODY()
	
	ABasePlayer();

	// ?쒕쾭?먯꽌 鍮숈쓽????ASC 珥덇린??
	virtual void PossessedBy(AController* NewController) override;

	// ?대씪?댁뼵?몄뿉??PlayerState媛 蹂듭젣 ?꾨즺?섏뿀????ASC 珥덇린??
	virtual void OnRep_PlayerState() override;

	// ?뚮젅?댁뼱 ?낅젰 諛붿씤??
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
};
