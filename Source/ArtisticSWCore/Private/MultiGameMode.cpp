// Fill out your copyright notice in the Description page of Project Settings.


#include "MultiGameMode.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"

void AMultiGameMode::PostLogin(APlayerController* NewPlayer)
{
	// 첫 번째 플레이어는 Attacker, 두 번째 플레이어는 Crafter
    GetOrAssignRole(NewPlayer);

    Super::PostLogin(NewPlayer);
}

UClass* AMultiGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
    // 배정된 역할에 따라 다른 폰 클래스를 반환합니다.
    const FName AssignedRole = GetOrAssignRole(InController);
    if (!AssignedRole.IsNone())
    {
        if (AssignedRole == FName("Attacker") && AttackerPawnClass)
        {
            return AttackerPawnClass;
        }
        else if (AssignedRole == FName("Crafter") && CrafterPawnClass)
        {
            return CrafterPawnClass;
        }
    }

	UE_LOG(LogTemp, Warning, TEXT("No role found for controller %s or corresponding pawn class not set."), *InController->GetName());
    // 예외 상황일 경우 기본 폰 반환
    return Super::GetDefaultPawnClassForController_Implementation(InController);
}

AActor* AMultiGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
    const FName TargetTag = GetOrAssignRole(Player);

    // 플레이어의 역할을 확인하고 그에 맞는 태그를 찾습니다.
    // 레벨에 있는 PlayerStart들을 순회하며 태그가 일치하는 곳을 반환합니다.
    for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
    {
        APlayerStart* PlayerStart = *It;
        if (PlayerStart->PlayerStartTag == TargetTag)
        {
            return PlayerStart;
        }
    }

    // 일치하는 태그가 없으면 엔진 기본 로직 수행
    return Super::ChoosePlayerStart_Implementation(Player);
}

FName AMultiGameMode::GetOrAssignRole(AController* Controller)
{
    if (!Controller)
    {
        return NAME_None;
    }

    if (const FName* FoundRole = PlayerRoles.Find(Controller))
    {
        return *FoundRole;
    }

    const FName AssignedRole = PlayerRoles.Num() == 0 ? FName("Attacker") : FName("Crafter");
    PlayerRoles.Add(Controller, AssignedRole);
    return AssignedRole;
}
