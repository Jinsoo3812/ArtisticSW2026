// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BaseWeaponComponent.generated.h"

struct FGameplayAbilitySpecHandle;
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


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class ENEMY_API UBaseWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBaseWeaponComponent();

protected:
	virtual void BeginPlay() override;

	// Weapon의 DA 초기에 무기를 Spawn
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UWeaponDataAsset> DefaultWeaponData;
	
	// ------런타임에 변경될 변수들------
	
	// 현재 Weapon
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentWeapon, Category = "Weapon")
	TObjectPtr<ABaseWeapon> CurrentWeapon = nullptr;

	// 현재 Weapon의 상태
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_WeaponState, Category = "Weapon")
	EEnemyWeaponState WeaponState = EEnemyWeaponState::None;

	// 부여받은 GA를 저장하는 SpecHandle
	UPROPERTY()
	TArray<FGameplayAbilitySpecHandle> GrantedAbilityHandles;

protected:
	// 현재 무기의 변경을 감지하는 RepNotify 함수
	UFUNCTION()
	void OnRep_CurrentWeapon();

	// 현재 무기의 상태 변경을 감지하는 RepNotify 함수
	UFUNCTION()
	void OnRep_WeaponState();

	// Owner Getter함수
	ABaseEnemy* GetOwningEnemy() const;

	// SocketName을 받아서 무기를 해당 Socket에 Attach하는 함수
	void AttachWeaponToSocket(const FName& SocketName);
	
	// AttachWeaponToSocket함수를 활용하여 BackSocket과 EquipSocket에 무기를 Attach하는 함수
	void AttachWeaponToBack();
	void AttachWeaponToEquipSocket();
	
	// 무기 상태나 무기가 바뀌었을 때 RepNotify함수에서 호출
	void SyncWeaponAttachment();

	// 무기에 들어있는 Ability들을 부여/제거 하는 함수
	void GrantWeaponAbilities();
	void ClearWeaponAbilities();

public:
	// WeaponComponent의 초기화함수
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void InitializeLoadout();

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void EquipCurrentWeapon();

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void UnequipCurrentWeapon();

	/*UFUNCTION(BlueprintCallable, Category = "Weapon")
	void DestroyCurrentWeapon()*/;

	// BlueprintPure 값만 꺼내오는 Getter함수들
	UFUNCTION(BlueprintPure, Category = "Weapon")
	ABaseWeapon* GetCurrentWeapon() const { return CurrentWeapon; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	UWeaponDataAsset* GetDefaultWeaponData() const { return DefaultWeaponData; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsWeaponEquipped() const { return WeaponState == EEnemyWeaponState::Equipped; }

	// RepNotify함수에서 호출되는 함수들
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
