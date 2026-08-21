// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BaseGameplayTags.h"

#include "BaseWeaponComponent.generated.h"

struct FGameplayAbilitySpecHandle;
struct FWeaponDefinition;

class ABaseWeapon;
class UWeaponDataAsset;
class ABaseEnemy;

// 무기의 상태를 지정하는 ENUM
UENUM(BlueprintType)
enum class EEnemyWeaponState : uint8
{
	None      UMETA(DisplayName = "None"),
	Holstered UMETA(DisplayName = "Holstered"),
	Equipped  UMETA(DisplayName = "Equipped")
};

/** Replicated owner-lifetime state for the separately spawned weapon actor. */
UENUM(BlueprintType)
enum class EEnemyWeaponLifecycleState : uint8
{
	Active        UMETA(DisplayName = "Active"),
	DeathInactive UMETA(DisplayName = "Death Inactive"),
	Pooled        UMETA(DisplayName = "Pooled")
};


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class ENEMY_API UBaseWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
	friend class FRangedEnemyAttackIntegrationTest;
#endif

public:
	UBaseWeaponComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Weapon의 DA 초기에 무기를 Spawn
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UWeaponDataAsset> WeaponRegistry = nullptr;
	
	// ------런타임에 변경될 변수들------
	
	// 현재 장착 중인 무기 태그
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentWeaponTag, Category = "Weapon")
	FGameplayTag  CurrentWeaponTag;
	
	// 현재 Weapon Actor
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentWeapon, Category = "Weapon")
	TObjectPtr<ABaseWeapon> CurrentWeapon = nullptr;

	// 현재 Weapon의 상태
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_WeaponState, Category = "Weapon")
	EEnemyWeaponState WeaponState = EEnemyWeaponState::None;

	/** One replicated state drives gameplay shutdown and presentation on every peer. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_WeaponLifecycleState, Category = "Weapon|Lifecycle")
	EEnemyWeaponLifecycleState WeaponLifecycleState = EEnemyWeaponLifecycleState::Active;

	// 무기로 인해 부여된 Ability Handle
	UPROPERTY()
	TArray<FGameplayAbilitySpecHandle> GrantedAbilityHandles;

protected:
	// ----------------- Network
	
	// 입력받은 무기 Tag의 변경을 감지하는 RepNotify 함수
	UFUNCTION()
	void OnRep_CurrentWeaponTag();
	
	// 현재 무기의 변경을 감지하는 RepNotify 함수
	UFUNCTION()
	void OnRep_CurrentWeapon();

	// 현재 무기의 상태 변경을 감지하는 RepNotify 함수
	UFUNCTION()
	void OnRep_WeaponState();

	UFUNCTION()
	void OnRep_WeaponLifecycleState();
	
	// ----------------- Attach 함수
	// SocketName을 받아서 무기를 해당 Socket에 Attach하는 함수
	void AttachWeaponToSocket(const FName& SocketName);
	
	// AttachWeaponToSocket함수를 활용하여 BackSocket과 EquipSocket에 무기를 Attach하는 함수
	void AttachWeaponToBack();
	void AttachWeaponToEquipSocket();

	// 무기 상태나 무기가 바뀌었을 때 RepNotify함수에서 호출
	void SyncWeaponAttachment();
	void ApplyWeaponLifecyclePresentation();

	// ----------------- WeaponData 관리
	const FWeaponDefinition* ResolveWeaponDefinition(FGameplayTag InTag) const;
	
	// ----------------- Ability 관리
	// 무기에 들어있는 Ability들을 부여/제거 하는 함수
	void GrantWeaponAbilities();
	void ClearWeaponAbilities();

public:
	// WeaponComponent의 초기화함수
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void InitializeLoadout(FGameplayTag InWeaponTag);

	/** Spawns the loadout on the back without granting its attack abilities yet. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void InitializeHolsteredLoadout(FGameplayTag InWeaponTag);

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void EquipCurrentWeapon();

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void UnequipCurrentWeapon();

	/** Stops attacks and granted abilities while keeping the weapon visible on the corpse. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Lifecycle")
	void DeactivateForOwnerDeath();

	/** Stops weapon gameplay and hides the replicated weapon actor for owner pooling. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Pooling")
	void SuspendForOwnerPool();

	/** Restores the existing weapon actor and its granted abilities after pooling. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Pooling")
	void RestoreFromOwnerPool();

	/** Permanently destroys the server-owned replicated weapon and clears its loadout state. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Lifecycle")
	void DestroyCurrentWeapon();

	// ------------------- Getter함수
	// BlueprintPure 값만 꺼내오는 Getter함수들
	UFUNCTION(BlueprintPure, Category = "Weapon")
	ABaseWeapon* GetCurrentWeapon() const { return CurrentWeapon; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	UWeaponDataAsset* GetWeaponRegistry() const { return WeaponRegistry; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	const FGameplayTag& GetCurrentWeaponTag() const { return CurrentWeaponTag; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsWeaponEquipped() const { return WeaponState == EEnemyWeaponState::Equipped; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Lifecycle")
	EEnemyWeaponLifecycleState GetWeaponLifecycleState() const { return WeaponLifecycleState; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Pooling")
	bool IsPoolPresentationActive() const { return WeaponLifecycleState != EEnemyWeaponLifecycleState::Pooled; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	float GetCurrentAttackRange() const;

	const FWeaponDefinition* GetCurrentWeaponDefinition() const;
	
	// Owner Getter함수
	ABaseEnemy* GetOwningEnemy() const;
	
	// RepNotify함수에서 호출되는 함수들
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void InitializeLoadoutInternal(FGameplayTag InWeaponTag, bool bEquipImmediately);
	void StopWeaponGameplay();
	void SetWeaponLifecycleState(EEnemyWeaponLifecycleState NewState);
};
