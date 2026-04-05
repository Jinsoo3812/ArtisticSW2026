// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MultiGameMode.generated.h"

/**
 * 처음에 Attacker 하나, Crafter 하나로 시작하기 위한 임시 게임모드
 */
UCLASS()
class ARTISTICSWCORE_API AMultiGameMode : public AGameModeBase
{
	GENERATED_BODY()
	
public:
    // 블루프린트에서 Attacker와 Crafter 클래스를 할당할 수 있게
    UPROPERTY(EditDefaultsOnly, Category = "Classes")
    TSubclassOf<APawn> AttackerPawnClass;

    UPROPERTY(EditDefaultsOnly, Category = "Classes")
    TSubclassOf<APawn> CrafterPawnClass;

protected:
    // 플레이어가 월드에 들어올 때 호출
    virtual void PostLogin(APlayerController* NewPlayer) override;

    // 해당 컨트롤러에게 어떤 폰 클래스를 스폰해줄지 결정
    virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;

    // 해당 플레이어를 어느 PlayerStart에서 스폰시킬지 결정.
    virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

private:
    // 각 컨트롤러가 어떤 역할을 배정받았는지 추적하는 맵
    TMap<AController*, FName> PlayerRoles;
};
