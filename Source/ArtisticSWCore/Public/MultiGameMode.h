// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MultiGameMode.generated.h"


class AController;
class APlayerController;
class APlayerStart;
class APawn;
class UPlayerRespawnPointComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnSWPlayerRoleAssigned,
    AController*, Controller,
    FName, AssignedRole,
    int32, PlayerIndex
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnSWPlayerReadyChanged,
    AController*, Controller,
    bool, bIsReady
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSWRequiredPlayersJoined);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSWAllPlayersReady);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSWGameOverRequested);
/**
 * 2인 멀티 플레이 기본 GameMode.
 *
 * 책임:
 * - 플레이어 접속 수 제한
 * - Attacker / Crafter 역할 배정
 * - 역할별 PawnClass 선택
 * - 역할별 PlayerStart 선택
 * - Ready 체크
 **/

UCLASS()
class ARTISTICSWCORE_API AMultiGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
    AMultiGameMode();

public:
    // ================================
    // Events
    // ================================

    UPROPERTY(BlueprintAssignable, Category = "Multiplayer|Events")
    FOnSWPlayerRoleAssigned OnPlayerRoleAssigned;

    UPROPERTY(BlueprintAssignable, Category = "Multiplayer|Events")
    FOnSWPlayerReadyChanged OnPlayerReadyChanged;

    UPROPERTY(BlueprintAssignable, Category = "Multiplayer|Events")
    FOnSWRequiredPlayersJoined OnRequiredPlayersJoined;

    UPROPERTY(BlueprintAssignable, Category = "Multiplayer|Events")
    FOnSWAllPlayersReady OnAllPlayersReady;

	/** Future defeat-screen hook. Currently followed immediately by a level reload. */
	UPROPERTY(BlueprintAssignable, Category = "Game Rules|Events")
	FOnSWGameOverRequested OnGameOverRequested;
	
public:
    /** 두 플레이어 모두에게 공통으로 스폰할 표준 플레이어 폰 클래스 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Classes")
    TSubclassOf<APawn> CommonPlayerPawnClass;

    // ====================================================================
    // [LEGACY / DEPRECATED] Attacker / Crafter 역할 분기는 더 이상 사용되지 않습니다.
    // ====================================================================
    UPROPERTY(EditDefaultsOnly, Category = "LEGACY|Roles", meta = (DeprecatedProperty, DeprecationMessage = "Use CommonPlayerPawnClass instead"))
    TSubclassOf<APawn> AttackerPawnClass;

    UPROPERTY(EditDefaultsOnly, Category = "LEGACY|Roles", meta = (DeprecatedProperty, DeprecationMessage = "Use CommonPlayerPawnClass instead"))
    TSubclassOf<APawn> CrafterPawnClass;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "LEGACY|Roles", meta = (DeprecatedProperty))
    FName AttackerRoleName = TEXT("Attacker");

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "LEGACY|Roles", meta = (DeprecatedProperty))
    FName CrafterRoleName = TEXT("Crafter");

    /** 게임 시작에 필요한 플레이어 수 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Multiplayer|Rules", meta = (ClampMin = "1"))
    int32 RequiredPlayerCount = 2;

    /** 서버에 들어올 수 있는 최대 플레이어 수 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Multiplayer|Rules", meta = (ClampMin = "1"))
    int32 MaxPlayerCount = 2;

    /**
     * true면 모든 플레이어가 Ready를 눌러야 HandleAllPlayersReady가 호출된다.
     * false면 RequiredPlayerCount만 채워져도 바로 준비 완료로 취급한다.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Multiplayer|Rules")
    bool bRequireAllPlayersReady = false;

    /**
     * 테스트용 옵션.
     * true면 PostLogin 직후 자동 Ready 처리된다.
     * PIE 테스트 중 Ready UI가 아직 없을 때 유용하다.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Multiplayer|Rules")
    bool bAutoReadyOnPostLogin = false;

protected:
    virtual void PreLogin(
        const FString& Options,
        const FString& Address,
        const FUniqueNetIdRepl& UniqueId,
        FString& ErrorMessage
    ) override;
    
    // 플레이어가 월드에 들어올 때 호출
    virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

    // 해당 컨트롤러에게 어떤 폰 클래스를 스폰해줄지 결정
    virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;

    // 해당 플레이어를 어느 PlayerStart에서 스폰시킬지 결정.
    virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

public:
    // ================================
    // Public API - PlayerController / UI / GameState가 GameMode에 상태를 요청하기 위해
    // ================================

    /** PlayerController의 Server RPC에서 호출하는 것을 권장 */
    UFUNCTION(BlueprintCallable, Category = "Multiplayer|Ready")
    void SetPlayerReady(AController* Controller, bool bReady);

    UFUNCTION(BlueprintPure, Category = "Multiplayer|Ready")
    bool IsPlayerReady(AController* Controller) const;

    UFUNCTION(BlueprintPure, Category = "Multiplayer|Role")
    FName GetPlayerRole(AController* Controller) const;

    UFUNCTION(BlueprintPure, Category = "Multiplayer|State")
    int32 GetConnectedPlayerCount() const;

    UFUNCTION(BlueprintPure, Category = "Multiplayer|State")
    bool AreRequiredPlayersJoined() const;

    UFUNCTION(BlueprintPure, Category = "Multiplayer|State")
    bool AreAllPlayersReady() const;

	UFUNCTION(BlueprintPure, Category="Respawn")
	int32 GetPlayerIndex(AController* Controller) const;

	UFUNCTION(BlueprintCallable, Category="Respawn")
	void NotifyPlayerDeathFinished(APawn* DeadPawn);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Respawn", meta=(ClampMin="0.0"))
	float IndividualRespawnDelay = 5.0f;
    
protected:
    // ================================
    // Extension Hooks
    // ================================

    /**
     * RequiredPlayerCount가 처음 충족되었을 때 호출된다.
     * WaveGameMode 같은 하위 클래스에서 override 가능.
     */
    virtual void HandleRequiredPlayersJoined();

    /**
     * RequiredPlayerCount 충족 + Ready 조건 충족 시 호출된다.
     * 상위 클래스 세팅 끝
     */
    virtual void HandleAllPlayersReady();

    /** 현재 상태를 보고 RequiredPlayersJoined / AllPlayersReady 이벤트를 발생시킬지 판단 */
    void TryNotifyReadinessState();

    /** PlayerIndex에 따라 역할을 결정한다. 0 = Attacker, 1 = Crafter */
    virtual FName GetRoleForPlayerIndex(int32 PlayerIndex) const;

    /** Controller에게 역할을 배정한다. */
    void AssignRoleToPlayer(AController* Controller);

    /** PlayerStartTag와 역할명이 일치하는 PlayerStart를 찾는다. */
    APlayerStart* FindPlayerStartByRole(FName RoleName) const;
    
protected:
    // ================================
    // Runtime
    // ================================

    /** 각 Controller의 역할 */
    TMap<TObjectPtr<AController>, FName> PlayerRoles;
	TMap<TObjectPtr<AController>, int32> PlayerIndices;
	TSet<TObjectPtr<AController>> FinishedDeadPlayers;
	TMap<TObjectPtr<AController>, FTimerHandle> RespawnTimers;

    /** Ready 상태인 Controller 목록 */
    TSet<TObjectPtr<AController>> ReadyPlayers;

    bool bRequiredPlayersJoinedNotified = false;
    bool bAllPlayersReadyNotified = false;

	void TryRespawnPlayer(AController* Controller);
	UPlayerRespawnPointComponent* FindShipRespawnPoint(int32 PlayerIndex) const;
	virtual void HandleAllPlayersDeathFinished();
};
