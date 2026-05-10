// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayEffectTypes.h"
#include "Trap.generated.h"

class UStaticMeshComponent;
class UAbilitySystemComponent;

UCLASS()
class CLASSFEATURE_API ATrap : public AActor
{
	GENERATED_BODY()

public:
	ATrap();

	// GA_InstallTrap에서 지연 생성(Deferred) 시 주입해 줄 GE Spec
	UPROPERTY(BlueprintReadWrite, meta = (ExposeOnSpawn = true), Category = "Trap")
	FGameplayEffectSpecHandle DamageEffectSpecHandle;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComp;

	UFUNCTION()
	virtual void OnTrapBeginOverlap(UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	virtual void OnTrapEndOverlap(UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

private:
	// 현재 밟고 있는 대상들의 활성화된 GE 핸들 보관 (나갈 때 지워주기 위함)
	TMap<UAbilitySystemComponent*, FActiveGameplayEffectHandle> ActiveDamageEffects;
};
