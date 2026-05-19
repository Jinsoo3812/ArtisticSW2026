// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "WaveSystem/Data/WaveSpawnTypes.h"
#include "BaseCharacter.h"

#include "BaseEnemy.generated.h"

class UAbilitySystemComponent;
class UPathMovement;
class UBaseWeaponComponent;
class UEnemyWaypointMoveComponent;
class AEnemyPathActor;
class AEnemySpawnPoint;

class UGameplayAbility;
class UBehaviorTree;
class ABaseAIController;
class ABaseWeapon;
class UWeaponDataAsset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBaseEnemyDeathNotifiedSignature, ABaseEnemy*, Enemy, EWaveEnemyRemoveReason, Reason);

UCLASS()
class ENEMY_API ABaseEnemy : public ABaseCharacter
{
	GENERATED_BODY()

protected:
	// ------------------ GAS

	// AttributeSet.h에서는 상위 Class 선언 cpp에서 실제 BaseAttributeSet으로 DownCast
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	TObjectPtr<class UBaseAttributeSet> BasicAttributes;

	// Blueprint에서 GrantAbility함수를 만들어서 사용했을 때, Server에서만 작동하는 문제가 있어서 C++에서 미리 선언해두는 방식으로 변경
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AbilitySystem")
	TArray<TSubclassOf<UGameplayAbility>> StartingAbilities;

	// ------------------ Enemy AI
	
	// Enemy에게 장착된 AI Controller
	UPROPERTY()
	TObjectPtr<ABaseAIController> AIController;
	
	// Enemy가 사용할 Behavior Tree
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "AI | Behavior Tree")
	TObjectPtr<UBehaviorTree> BehaviorTree;

	// ------------------- WeaponTag

	// Enemy가 가지고 시작할 무기 Tag
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Weapon")
	FGameplayTag DefaultWeaponTag;
	
	// ------------------- Componenet

	// WeaponComponent
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UBaseWeaponComponent> WeaponComponent = nullptr;
	
	// Path
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Path")
    TObjectPtr<UPathMovement> PathMovement = nullptr;
	
	// WaypointMove
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UEnemyWaypointMoveComponent> WaypointMoveComponent;

	// ------------------- GameMode
	// Death 중복 방지
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Damage")
	bool bDeathHandled = false;
	
public:
	ABaseEnemy();

protected:
	// Ability를 ASC Owner에 부여하는 함수
	UFUNCTION(BlueprintCallable, Category = "AbilitySystem")
	TArray<FGameplayAbilitySpecHandle> GrantAbilities(TArray<TSubclassOf<UGameplayAbility>> AbilitiesToGrant);

	/*// Ability를 ASC Owner에서 제거하는 함수 추후 활용 생각해봄
	UFUNCTION(BlueprintCallable, Category = "AbilitySystem")
	void RemoveAbilities(TArray<FGameplayAbilitySpecHandle> AbilityHandlesToRemove);
	*/
	
	// Ability가 변했다는 것을 알리는 함수
	/*UFUNCTION(BlueprintCallable, Category = "AbilitySystem")
	void SendAbilitiesChangedEvent();

	// Multi환경에서 Client가 Server에 GameplayEvent를 전송해 ASC에 적용하도록하는 함수
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "AbilitySystem")
	void ServerSendGameplayEventToSelf(FGameplayEventData EventData);*/
	
	// ASC Owner가 죽었을 때 OnDeadTagChanged에서 호출되는 함수
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Damage")
	void HandleDeath();

protected:
	virtual void BeginPlay() override;
	
	//  ASC Owner가 State.Dead Tag를 가질 때, 호출되는 함수
	virtual void OnDeadTagChanged(const FGameplayTag CallbackTag, int32 NewCount);

	FVector GetVelocity() const override;
public:
	UFUNCTION(BlueprintCallable, Category="Path")
	void InitializePathMovement(AEnemyPathActor* InPath, float InStartDistance, bool bStartImmediately = true);

	UFUNCTION(BlueprintCallable, Category="Path")
	void InitializePathMovementFromSpawnPoint(AEnemySpawnPoint* InSpawnPoint, bool bStartImmediately = true);

	/**
	* Enemy Death를 외부 Wave 시스템에 알리는 Delegate.
	* - AWaveSpawnManager가 이 Delegate에 바인딩해서 AliveEnemyCount를 감소시킨다.
	*/
	UPROPERTY(BlueprintAssignable, Category = "Wave|Events")
	FOnBaseEnemyDeathNotifiedSignature OnBaseEnemyDeathNotified;
public:
	// Getters
	// 일단 개발 중이므로, check를 넣었지만, 일부 BeginPlay 이전에는 nullptr 날 수 있음
	FORCEINLINE TObjectPtr<ABaseAIController> GetAIController() const { check(AIController) return AIController; }
	FORCEINLINE TObjectPtr<UBehaviorTree> GetBehaviorTree() const { return BehaviorTree; }
	FORCEINLINE FGameplayTag GetDefaultWeaponTag() const { return DefaultWeaponTag; }
	FORCEINLINE TObjectPtr<UBaseWeaponComponent> GetWeaponComponent() const { check(WeaponComponent) return WeaponComponent; }
	FORCEINLINE TObjectPtr<UPathMovement> GetPathMovementComponent() const { check(PathMovement) return PathMovement;}
	FORCEINLINE virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { check(AbilitySystemComponent) return AbilitySystemComponent; }
	FORCEINLINE UEnemyWaypointMoveComponent* GetWaypointMoveComponent() const {return WaypointMoveComponent;}
};
