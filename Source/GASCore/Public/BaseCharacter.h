// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "BaseCharacter.generated.h"

UCLASS()
class GASCORE_API ABaseCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ABaseCharacter();

	// 공통 AbilitySystemComponent
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	// 공통 AttributeSet.h에서는 상위 Class 선언 cpp에서 실제 BaseAttributeSet으로 DownCast
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	TObjectPtr<class UBaseAttributeSet> BasicAttributes;

protected:
	/*
	 * AbilitySystemComponent가 네트워크 환경에서 어떻게 복제될지를 결정하는 설정, 필요시, 자식클래스에서 Override해서 사용
	 * Mixed: 능력과 GE가 모두 서버에서 복제되고, 클라이언트는 필요한 정보를 서버로부터 받아서 처리하는 방식. Player 캐릭터에 적합
	 * Minimal: 능력과 GE가 모두 서버에서 복제되지 않고, 클라이언트는 필요한 정보를 서버로부터 받아서 처리하는 방식. Enemy 캐릭터에 적합
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AbilitySystem")
	EGameplayEffectReplicationMode ASCReplicationMode = EGameplayEffectReplicationMode::Mixed;

	// Character가 공통으로 가지는 Ability (사망, 피격)
	// Blueprint에서 GrantAbility함수를 만들어서 사용했을 때, Server에서만 작동하는 문제가 있어서 C++에서 미리 선언해두는 방식으로 변경
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AbilitySystem")
	TArray<TSubclassOf<UGameplayAbility>> StartingAbilities;

protected:
	// Ability를 ASC Owner에 부여하는 함수
	UFUNCTION(BlueprintCallable, Category = "AbilitySystem")
	TArray<FGameplayAbilitySpecHandle> GrantAbilities(TArray<TSubclassOf<UGameplayAbility>> AbilitiesToGrant);

	// Ability를 ASC Owner에서 제거하는 함수
	UFUNCTION(BlueprintCallable, Category = "AbilitySystem")
	void RemoveAbilities(TArray<FGameplayAbilitySpecHandle> AbilityHandlesToRemove);
	
	// Ability가 변했다는 것을 알리는 함수
	UFUNCTION(BlueprintCallable, Category = "AbilitySystem")
	void SendAbilitiesChangedEvent();

	// Multi환경에서 Client가 Server에 GameplayEvent를 전송해 ASC에 적용하도록하는 함수
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "AbilitySystem")
	void ServerSendGameplayEventToSelf(FGameplayEventData EventData);
	
	// ASC Owner가 죽었을 때 OnDeadTagChanged에서 호출되는 함수
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Damage")
	void HandleDeath();

	
	// 죽음과 관련된 Tag가 변경될 때마다 호출되는 함수
	// 공통으로 가지는 요소만 구현, 디테일은 override
	virtual void OnDeadTagChanged(const FGameplayTag CallbackTag, int32 NewCount);
	
public:
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	
public:
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void Tick(float DeltaTime) override;
	
	
	/*
	 *플레이어에게는 필요하지만 Enemy에게는 필요없음
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	*/
};
