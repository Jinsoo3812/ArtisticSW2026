// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpec.h"
#include "AttackerComponent.generated.h"

class UGameplayAbility;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;
class UInputTagConfig;
class ABasePlayer;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CLASSFEATURE_API UAttackerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAttackerComponent();

protected:
	virtual void BeginPlay() override;

	/* --- IMC 추가 및 입력 바인딩 --- */
public:
	// Attacker 전용 입력 매핑 컨텍스트
	UPROPERTY(EditDefaultsOnly, Category = "Attacker|Input")
	UInputMappingContext* AttackerIMC;

	// IMC의 우선순위
	UPROPERTY(EditDefaultsOnly, Category = "Attacker|Input")
	int32 AttackerIMCPriority = 2;

	// Attacker 전용 입력 설정 (IA <-> Tag)
	UPROPERTY(EditDefaultsOnly, Category = "Attacker|Input")
	UInputTagConfig* AttackerInputConfig;

	// Player의 SetupPlayerInputComponent 시점에 호출하여 Attacker 전용 입력을 바인딩
	void BindAttackerInput(UEnhancedInputComponent* EnhancedInputComponent);

	// Player에게 Attacker 전용 IMC를 추가/제거
	void AddAttackerMappingContext();
	void RemoveAttackerMappingContext();

	/* --- Player에게 부여하는 GA 관리 --- */
protected:
	// 서버에서 어빌리티를 부여할 때 호출합니다.
	void GrantAttackerAbilities();

	// 컴포넌트 제거 시 부여된 어빌리티를 회수합니다.
	void RemoveAttackerAbilities();

	// Attacker 전용 어빌리티와 고정 Slot Tag의 매핑 정보
	UPROPERTY(EditDefaultsOnly, Category = "Attacker|Abilities")
	TMap<FGameplayTag, TSubclassOf<UGameplayAbility>> AttackerAbilities;
};