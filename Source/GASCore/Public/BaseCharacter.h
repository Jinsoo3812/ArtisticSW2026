// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "BaseCharacter.generated.h"

UCLASS()
class GASCORE_API ABaseCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ABaseCharacter();

	// 이 캐릭터가 직접 소유하거나 외부(PlayerState 등)에서 캐시해 온 AbilitySystemComponent입니다.
	// 플레이어는 보통 PlayerState ASC를 캐시하고, 적은 캐릭터가 ASC를 직접 생성합니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

protected:
	// ASC가 GameplayEffect, GameplayTag, GameplayCue를 네트워크에 복제하는 방식을 정합니다.
	// Mixed는 플레이어처럼 소유 클라이언트가 있는 액터에 적합하고, Minimal은 일반 적처럼 세부 GE 복제가 덜 필요한 액터에 적합합니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AbilitySystem")
	EGameplayEffectReplicationMode ASCReplicationMode = EGameplayEffectReplicationMode::Mixed;

public:
	// IAbilitySystemInterface 구현입니다. AbilitySystemBlueprintLibrary가 이 함수로 ASC를 찾습니다.
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	
public:
	// 캐릭터 공통 초기화 지점입니다. 현재는 파생 클래스 초기화를 방해하지 않도록 기본 호출만 수행합니다.
	virtual void BeginPlay() override;

	// 매 프레임 캐릭터 공통 처리를 수행하는 지점입니다. 현재는 기본 Tick만 호출합니다.
	virtual void Tick(float DeltaTime) override;
};
