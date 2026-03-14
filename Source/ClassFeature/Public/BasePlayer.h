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

	// 서버에서 빙의될 때 ASC 초기화
	virtual void PossessedBy(AController* NewController) override;

	// 클라이언트에서 PlayerState가 복제 완료되었을 때 ASC 초기화
	virtual void OnRep_PlayerState() override;

	// 플레이어 입력 바인딩
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
};
