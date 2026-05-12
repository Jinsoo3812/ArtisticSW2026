// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_InstallTrap.generated.h"

class AGhostMeshActor;

UCLASS()
class CLASSFEATURE_API UGA_InstallTrap : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_InstallTrap();

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	// 입력 해제 시 즉시 설치
	UFUNCTION()
	void OnInputReleased(float TimeHeld);

	// 우클릭 취소
	UFUNCTION()
	void OnRightClickCancelled(FGameplayEventData Payload);

	// 매 주기마다 트레이스를 쏘고 고스트 메쉬 위치 업데이트
	UFUNCTION()
	void UpdateInstallTarget();

	// 트레이스 및 유효성 검사 로직 (Client/Server 공용)
	bool CheckInstallLocation(FVector& OutLocation, FRotator& OutRotation);

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trap")
	TSubclassOf<class UGameplayEffect> DamageEffectClass;

	UPROPERTY(EditDefaultsOnly, Category = "Trap")
	TSubclassOf<AGhostMeshActor> GhostActorClass;

	UPROPERTY(EditDefaultsOnly, Category = "Trap")
	float InstallRange = 1000.0f; // 최대 설치 거리

	UPROPERTY(EditDefaultsOnly, Category = "Trap")
	float UpdateFrequency = 0.05f; // 고스트 메쉬 업데이트 주기

	// 현재 스폰된 고스트 메쉬 인스턴스 (로컬 클라이언트에서만 유효)
	UPROPERTY()
	TObjectPtr<AGhostMeshActor> SpawnedGhostActor;

	FTimerHandle TargetTimerHandle;

	// 최후에 확인된 설치 가능 여부 및 위치 캐싱
	bool bIsCurrentPositionValid;
	FTransform TargetTransform;
};
