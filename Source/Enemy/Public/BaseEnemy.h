// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "BaseCharacter.h"
#include "WaveSystem/Data/WaveSpawnTypes.h"
#include "EnemyDropData.h"
#include "UI/EnemyHealthBarTypes.h"

#include "BaseEnemy.generated.h"

class UAbilitySystemComponent;
class UBaseWeaponComponent;
class UBaseHealthComponent;
class UEnemyWaypointMoveComponent;
class UHealthBarWidget;
class UWidgetComponent;

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

public:
	ABaseEnemy();
	
	/**
	* Enemy Death를 외부 Wave 시스템에 알리는 Delegate.
	* - AWaveSpawnManager가 이 Delegate에 바인딩해서 AliveEnemyCount를 감소시킨다.
	*/
	UPROPERTY(BlueprintAssignable, Category = "Wave|Events")
	FOnBaseEnemyDeathNotifiedSignature OnBaseEnemyDeathNotified;

	UFUNCTION(BlueprintCallable, Category = "Wave")
	void NotifyRemovedFromWaveOnce(EWaveEnemyRemoveReason Reason);

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

	// WaypointMove
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UEnemyWaypointMoveComponent> WaypointMoveComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBaseHealthComponent> HealthComponent;

	// ================= Health Bar =================
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWidgetComponent> HealthBarWidgetComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HealthBar")
	TSubclassOf<UHealthBarWidget> HealthBarWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HealthBar")
	FVector HealthBarOffset = FVector(0.0f, 0.0f, 120.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HealthBar")
	FVector2D HealthBarDrawSize = FVector2D(180.0f, 24.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HealthBar")
	EEnemyHealthBarVisibilityPolicy HealthBarVisibilityPolicy = EEnemyHealthBarVisibilityPolicy::AlwaysVisible;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HealthBar", meta = (EditCondition = "HealthBarVisibilityPolicy == EEnemyHealthBarVisibilityPolicy::ShowOnDamage", ClampMin = "0.0"))
	float HealthBarVisibleDurationAfterDamage = 2.0f;

	// ================= End of Health Bar =================

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wave")
	bool bWaveRemoveNotified = false;

	// ------------------- GameMode
	// Death 중복 방지
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Damage")
	bool bDeathHandled = false;

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
	
	// ASC Owner가 죽었을 때 호출되는 함수
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Damage")
	void HandleDeath();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	// HealthComponent가 죽음을 감지했을 때 기존 Enemy 사망 처리를 실행합니다.
	UFUNCTION()
	void OnDeathStarted(UBaseHealthComponent* InHealthComponent);

	// ================= Health Bar =================
	UFUNCTION()
	void OnHealthChanged(UBaseHealthComponent* InHealthComponent, float OldValue, float NewValue, AActor* InstigatorActor);

	UFUNCTION()
	void OnMaxHealthChanged(UBaseHealthComponent* InHealthComponent, float OldValue, float NewValue, AActor* InstigatorActor);

	void InitializeHealthBarWidget();
	void RefreshHealthBarWidget();
	void UpdateHealthBarVisibilityAfterHealthChanged(float OldValue, float NewValue);
	void HideHealthBarForDamagePolicy();
	FTimerHandle HealthBarHideTimerHandle;
	// ================= End of Health Bar =================

	// FVector GetVelocity() const override;
	
public:
	// Getters
	// 일단 개발 중이므로, check를 넣었지만, 일부 BeginPlay 이전에는 nullptr 날 수 있음
	FORCEINLINE TObjectPtr<ABaseAIController> GetAIController() const { check(AIController) return AIController; }
	FORCEINLINE TObjectPtr<UBehaviorTree> GetBehaviorTree() const { return BehaviorTree; }
	FORCEINLINE FGameplayTag GetDefaultWeaponTag() const { return DefaultWeaponTag; }
	FORCEINLINE TObjectPtr<UBaseWeaponComponent> GetWeaponComponent() const { check(WeaponComponent) return WeaponComponent; }
	//FORCEINLINE TObjectPtr<UPathMovement> GetPathMovementComponent() const { check(PathMovement) return PathMovement;}
	FORCEINLINE virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { check(AbilitySystemComponent) return AbilitySystemComponent; }
	FORCEINLINE UEnemyWaypointMoveComponent* GetWaypointMoveComponent() const {return WaypointMoveComponent;}
	FORCEINLINE UBaseHealthComponent* GetHealthComponent() const { return HealthComponent; }

	// Enemy소환 API
	UFUNCTION(BlueprintCallable, Category = "Wave")
	void InitializeFromWaveSpawn(
		float HealthMultiplier,
		float SpeedMultiplier,
		int32 EnemyLevel
	);
	
	/*---- For Drop ----*/
	// 추후 위치 수정
protected:

	// 드랍 관련 데이터 테이블 (CSV 파일)
	UPROPERTY(EditDefaultsOnly, Category = "Drop")
	TObjectPtr<UDataTable> EnemyDropDataTable;

	// 적 종류를 구분하는 태그 -> 해당 태그로 데이터가 있는 Row 검색
	UPROPERTY(EditDefaultsOnly, Category = "Drop")
	FGameplayTag EnemyTypeTag;

	// 사망한 적의 위치에 생성할 시체 전용 Storage BP
	UPROPERTY(EditDefaultsOnly, Category = "Drop|Storage")
	TSubclassOf<class AStorageChest> EnemyCorpseStorageClass;

	// 기본 슬롯 수. 아이템 Entry 수가 더 많으면 자동 확장된다.
	UPROPERTY(EditDefaultsOnly, Category = "Drop|Storage", meta = (ClampMin = "1", UIMin = "1"))
	int32 EnemyCorpseStorageSlotCount = 5;

	UPROPERTY(EditDefaultsOnly, Category = "Drop|Storage", meta = (ClampMin = "1", UIMin = "1"))
	int32 EnemyCorpseStorageColumnCount = 4;

	// 적 하나가 드랍할 아이템들에 대한 정보를 담은 구조체
	UPROPERTY()
	FEnemyDropData EnemyDropData;

	// 한 번 드랍 했는지 확인하는 변수
	UPROPERTY()
	bool bHasDropped = false;

public:
	
	//Data Table의 전체 데이터중 자신에게 해당하는 데이터를 가져오는 함수
	void InitializeEnemyDropData();

	// 실제 아이템을 Drop하는 함수
	UFUNCTION(BlueprintCallable)
	void Drop();
	
};
