// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "BaseCharacter.generated.h"

UCLASS()
class GASCORE_API ABaseCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ABaseCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Death")
	virtual void ApplyLocalDeathRagdoll();

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AbilitySystem")
	EGameplayEffectReplicationMode ASCReplicationMode = EGameplayEffectReplicationMode::Mixed;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death|Ragdoll")
	bool bDisableCapsuleCollisionOnDeathRagdoll = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death|Ragdoll")
	bool bDisableMovementOnDeathRagdoll = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death|Ragdoll")
	bool bDetachControllerOnDeathRagdoll = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death|Ragdoll")
	bool bApplyDeathRagdollImpulse = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death|Ragdoll", meta = (ClampMin = "0.0"))
	float DeathRagdollBackwardImpulse = 20000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death|Ragdoll", meta = (ClampMin = "0.0"))
	float DeathRagdollUpwardImpulse = 15000.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Death|Ragdoll")
	bool bLocalDeathRagdollApplied = false;
};
