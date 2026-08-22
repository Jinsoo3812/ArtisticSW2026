// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "BaseCharacter.h"
#include "WaveSystem/Data/WaveSpawnTypes.h"
#include "EnemyDropData.h"
#include "UI/EnemyHealthBarTypes.h"
#include "StoryFacadeSubsystem.h"

#include "BaseEnemy.generated.h"

class UAbilitySystemComponent;
class UBaseDeathGameplayAbility;
class UBaseWeaponComponent;
class UBaseHealthComponent;
class UEnemyBehaviorSet;
class UEnemyWaypointMoveComponent;
class UHealthBarWidget;
class UWidgetComponent;
struct FOnAttributeChangeData;

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
	virtual bool IsEnemyCharacterForEffects() const override { return true; }
	
	/**
	* Enemy Death를 외부 Wave 시스템에 알리는 Delegate.
	* - AWaveSpawnManager가 이 Delegate에 바인딩해서 AliveEnemyCount를 감소시킨다.
	*/
	UPROPERTY(BlueprintAssignable, Category = "Wave|Events")
	FOnBaseEnemyDeathNotifiedSignature OnBaseEnemyDeathNotified;

	UFUNCTION(BlueprintCallable, Category = "Wave")
	void NotifyRemovedFromWaveOnce(EWaveEnemyRemoveReason Reason);

	/** 적(보스) 사망 시 자동으로 완료할 스토리 노드 설정 (중간보스 1/2/3, 최종보스 등) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Story")
	bool bCompleteStoryNodeOnDeath = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Story", meta = (EditCondition = "bCompleteStoryNodeOnDeath"))
	EStoryNode CompletedStoryNodeOnDeath = EStoryNode::MiddleBoss1Defeated;

protected:
	// ------------------ GAS

	// AttributeSet.h에서는 상위 Class 선언 cpp에서 실제 BaseAttributeSet으로 DownCast
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	TObjectPtr<class UBaseAttributeSet> BasicAttributes;

	// Blueprint에서 GrantAbility함수를 만들어서 사용했을 때, Server에서만 작동하는 문제가 있어서 C++에서 미리 선언해두는 방식으로 변경
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AbilitySystem")
	TArray<TSubclassOf<UGameplayAbility>> StartingAbilities;

	/**
	 * Optional death GA used by enemies that need a montage-driven death sequence.
	 * Regular enemies leave this empty and enter ragdoll immediately. Boss enemies
	 * opt in with their dedicated death ability.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Death")
	TSubclassOf<UBaseDeathGameplayAbility> DeathAbilityClass;

	// ------------------ Enemy AI
	
	// Enemy에게 장착된 AI Controller
	UPROPERTY()
	TObjectPtr<ABaseAIController> AIController;
	
	// Enemy가 사용할 Behavior Tree
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "AI | Behavior Tree")
	TObjectPtr<UBehaviorTree> BehaviorTree;

	/** State별 Run Behavior Dynamic Subtree를 설정하는 데이터 자산입니다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI | Behavior Tree")
	TObjectPtr<UEnemyBehaviorSet> BehaviorSet;

	/** If false, the default weapon is spawned on its back and equipped by combat behavior. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	bool bEquipWeaponOnSpawn = true;

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

	/** Base speed selected by the current locomotion mode before runtime modifiers. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Enemy|Movement")
	float BaseMovementSpeed = 0.0f;

	/** Wave/archetype scaling. Buffs remain additive after this multiplier. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Enemy|Movement")
	float SpawnMovementSpeedMultiplier = 1.0f;

	/** Safety cap for the resolved CharacterMovement MaxWalkSpeed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MaximumResolvedMovementSpeed = 2000.0f;

	/** Death presentation이 끝난 뒤 서버가 시체 Actor를 유지하는 시간입니다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Death", meta = (ClampMin = "0.0"))
	float CorpseLifetimeAfterDeathFinished = 5.0f;

	/** false이면 기존처럼 외부 시스템이 시체 Actor의 수명을 관리합니다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Death")
	bool bDestroyAfterDeathFinished = true;

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
	
	// DeathStarted 시점의 즉시 게임플레이 정리 훅입니다. 일반 Enemy는 이후 즉시 Ragdoll,
	// Death GA를 사용하는 Enemy는 DeathFinished에서 Ragdoll을 적용합니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Damage")
	void HandleDeath();

	/** Whether this enemy delays ragdoll until its death GA calls FinishDeath. */
	virtual bool ShouldWaitForDeathAbility() const;

	/** Death GA가 FinishDeath를 호출한 뒤 각 머신에서 사망 표현을 마무리합니다. */
	virtual void HandleDeathFinishedPresentation();

protected:
	virtual void BeginPlay() override;
	virtual void Destroyed() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	// HealthComponent가 죽음을 감지했을 때 기존 Enemy 사망 처리를 실행합니다.
	UFUNCTION()
	void OnDeathStarted(UBaseHealthComponent* InHealthComponent);

	UFUNCTION()
	void OnDeathFinished(UBaseHealthComponent* InHealthComponent);

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

	void BindMovementSpeedAttribute();
	void UnbindMovementSpeedAttribute();
	void OnMoveSpeedBonusChanged(const FOnAttributeChangeData& ChangeData);
	FDelegateHandle MoveSpeedBonusChangedDelegateHandle;

	// FVector GetVelocity() const override;
	
public:
	/**
	 * Perception 후보가 이 Enemy의 전투 대상이 될 수 있는지 검사합니다.
	 * 지상/갑판/보스 Enemy는 Blueprint 또는 C++에서 이 정책만 확장할 수 있습니다.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Enemy|AI")
	bool CanEngageActor(AActor* Candidate) const;

	// Getters
	// 일단 개발 중이므로, check를 넣었지만, 일부 BeginPlay 이전에는 nullptr 날 수 있음
	FORCEINLINE TObjectPtr<ABaseAIController> GetAIController() const { check(AIController) return AIController; }
	FORCEINLINE TObjectPtr<UBehaviorTree> GetBehaviorTree() const { return BehaviorTree; }
	FORCEINLINE UEnemyBehaviorSet* GetBehaviorSet() const { return BehaviorSet; }
	FORCEINLINE bool ShouldEquipWeaponOnSpawn() const { return bEquipWeaponOnSpawn; }
	FORCEINLINE FGameplayTag GetDefaultWeaponTag() const { return DefaultWeaponTag; }
	FORCEINLINE FGameplayTag GetEnemyTypeTag() const { return EnemyTypeTag; }
	FORCEINLINE TObjectPtr<UBaseWeaponComponent> GetWeaponComponent() const { check(WeaponComponent) return WeaponComponent; }
	//FORCEINLINE TObjectPtr<UPathMovement> GetPathMovementComponent() const { check(PathMovement) return PathMovement;}
	FORCEINLINE virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { check(AbilitySystemComponent) return AbilitySystemComponent; }
	FORCEINLINE UEnemyWaypointMoveComponent* GetWaypointMoveComponent() const {return WaypointMoveComponent;}
	FORCEINLINE UBaseHealthComponent* GetHealthComponent() const { return HealthComponent; }
	FORCEINLINE EGameplayEffectReplicationMode GetASCReplicationMode() const { return ASCReplicationMode; }
	FORCEINLINE float GetCorpseLifetimeAfterDeathFinished() const { return CorpseLifetimeAfterDeathFinished; }
	FORCEINLINE bool ShouldDestroyAfterDeathFinished() const { return bDestroyAfterDeathFinished; }
	FORCEINLINE TSubclassOf<UBaseDeathGameplayAbility> GetDeathAbilityClass() const { return DeathAbilityClass; }

	/** Sets the locomotion-mode speed. Only authority may drive Enemy movement policy. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Movement", BlueprintAuthorityOnly)
	void SetBaseMovementSpeed(float NewBaseSpeed);

	UFUNCTION(BlueprintPure, Category = "Enemy|Movement")
	float GetBaseMovementSpeed() const { return BaseMovementSpeed; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Movement")
	float GetSpawnMovementSpeedMultiplier() const { return SpawnMovementSpeedMultiplier; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Movement")
	float GetResolvedMovementSpeed() const;

	/** Pure resolver kept public for deterministic automation tests and balancing tools. */
	static float ResolveMovementSpeed(
		float InBaseSpeed,
		float InSpawnMultiplier,
		float InMoveSpeedBonus,
		float InMaximumSpeed);

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

	// 적/보스 종류를 구분하는 태그 (예: Enemy.Type.Boss.Mid1, Enemy.Type.Boss.Mid2 등)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Type")
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
