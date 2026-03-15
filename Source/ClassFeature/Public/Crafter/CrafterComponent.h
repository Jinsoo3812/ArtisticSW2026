// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpec.h"
#include "CrafterComponent.generated.h"

class UGameplayAbility;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;
class UInputTagConfig;
class ABasePlayer;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class CLASSFEATURE_API UCrafterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCrafterComponent();

protected:
	virtual void BeginPlay() override;

	/* --- IMC 추가 및 입력 바인딩 --- */
public:
	// Crafter 전용 입력 매핑 컨텍스트
	UPROPERTY(EditDefaultsOnly, Category = "Crafter|Input")
	UInputMappingContext* CrafterIMC;

	// IMC의 우선순위
	UPROPERTY(EditDefaultsOnly, Category = "Crafter|Input")
	int32 CrafterIMCPriority = 2;

	// Crafter 전용 입력 설정 (IA <-> Tag)
	UPROPERTY(EditDefaultsOnly, Category = "Crafter|Input")
	UInputTagConfig* CrafterInputConfig;

	// Player의 SetupPlayerInputComponent 시점에 호출하여 Crafter 전용 입력을 바인딩
	void BindCrafterInput(UEnhancedInputComponent* EnhancedInputComponent);

	// Player에게 Crafter 전용 IMC를 추가/제거
	void AddCrafterMappingContext();
	void RemoveCrafterMappingContext();

	/* --- Player에게 부여하는 GA 관리 --- */
protected:
	// 서버에서 어빌리티를 부여할 때 호출합니다.
	void GrantCrafterAbilities();

	// 컴포넌트 제거 시 부여된 어빌리티를 회수합니다.
	void RemoveCrafterAbilities();

	// Crafter 전용 어빌리티와 고정 Slot Tag의 매핑 정보 (ex. Crafer가 되면 F는 무조건 제작 GA)
	UPROPERTY(EditDefaultsOnly, Category = "Crafter|Abilities")
	TMap<FGameplayTag, TSubclassOf<UGameplayAbility>> CrafterAbilities;
};
