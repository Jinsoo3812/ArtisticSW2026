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
	/** WaterAndShip 등 저수준 모듈이 Enemy 모듈을 참조하지 않고 적을 판별할 수 있게 합니다. */
	virtual bool IsEnemyCharacterForEffects() const { return false; }
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Death")
	virtual void ApplyLocalDeathRagdoll();

	/** Restores the presentation state required before a pooled character is reused. */
	UFUNCTION(BlueprintCallable, Category = "Death|Pooling")
	virtual void ResetLocalDeathRagdoll();

	UFUNCTION(BlueprintPure, Category = "Death|Ragdoll")
	bool IsDeathRagdollImpulseEnabled() const { return bApplyDeathRagdollImpulse; }

	UFUNCTION(BlueprintPure, Category = "Death|Ragdoll")
	float GetDeathRagdollHorizontalImpulse() const { return DeathRagdollHorizontalImpulse; }

	UFUNCTION(BlueprintPure, Category = "Death|Ragdoll")
	float GetDeathRagdollUpwardImpulse() const { return DeathRagdollUpwardImpulse; }

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

	/** Prevents fast corpse bodies from tunneling through thin deck collision. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death|Ragdoll")
	bool bUseDeathRagdollCCD = true;

	/** Horizontal impulse magnitude applied in the lethal hit's replicated direction. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death|Ragdoll", meta = (ClampMin = "0.0"))
	float DeathRagdollHorizontalImpulse = 20000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death|Ragdoll", meta = (ClampMin = "0.0"))
	float DeathRagdollUpwardImpulse = 15000.0f;

	/** Physics body used when the reported hit bone has no simulated body. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death|Ragdoll")
	FName DeathRagdollFallbackImpulseBone = TEXT("pelvis");

	UPROPERTY(BlueprintReadOnly, Category = "Death|Ragdoll")
	bool bLocalDeathRagdollApplied = false;

	FTransform InitialMeshRelativeTransform = FTransform::Identity;
	FName InitialMeshCollisionProfileName = NAME_None;
	ECollisionEnabled::Type InitialMeshCollisionEnabled = ECollisionEnabled::NoCollision;
};
