// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "GameplayEffectTypes.h"

#include "BaseWeapon.generated.h"

struct FGameplayEffectSpecHandle;
class UWeaponDataAsset;
class USceneComponent;
class UAbilitySystemComponent;

UCLASS()
class ENEMY_API ABaseWeapon : public AActor
{
	GENERATED_BODY()

public:
	ABaseWeapon();

protected:
	// 무기의 Mesh, 이후 활과같은 ABP가 필요한 것들은 SkeletalMesh변수를 따로 생성
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	// 무기 Trace의 시작/끝 지점. Blueprint에서 WeaponMesh 기준으로 위치를 잡아준다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Trace")
	TObjectPtr<USceneComponent> TraceStartPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Trace")
	TObjectPtr<USceneComponent> TraceEndPoint;

	// Multi Sphere Trace For Objects 와 동일한 개념의 대상 설정
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Trace")
	TArray<TEnumAsByte<EObjectTypeQuery>> TraceObjectTypes;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Trace", meta = (ClampMin = "0.1"))
	float TraceRadius = 12.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Trace", meta = (ClampMin = "0.001"))
	float HitScanInterval = 0.02f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Trace")
	bool bTraceComplex = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Trace")
	bool bDrawDebugTrace = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Trace")
	bool bIsHitScanActive = false;

	// HitScan이 활성화될 때 GA로부터 받아오는 EffectSpecHandle, HitScan이 끝날 때 초기화
	FGameplayEffectSpecHandle CachedEffectSpecHandle = FGameplayEffectSpecHandle();

	// HitScan TimerHandle
	FTimerHandle HitScanTimerHandle;
	
	// HitScan이 활성화된 동안, 이미 Hit 처리한 Actor들을 저장하는 Set. HitScan이 끝날 때 초기화
	TSet<TWeakObjectPtr<AActor>> HitActors;

protected:
	void ProcessTrace();
	void HitScan(const FHitResult& HitResult);
	void ApplyEffectToTarget(AActor* TargetActor, const FHitResult& HitResult) const;
	bool ShouldIgnoreActor(const AActor* OtherActor) const;
	void ClearHitScanInternalState();
	
public:
	// GA로부터 EffectSpecHandle을 받아서 HitScan함수를 실행하는 함수
	UFUNCTION(BlueprintCallable, Category = "Weapon|Trace")
	virtual void HitScanStart(const FGameplayEffectSpecHandle& HitScanEffectSpecHandle);

	UFUNCTION(BlueprintCallable, Category = "Weapon|Trace")
	virtual void HitScanEnd();

public:
	// Getter
	FORCEINLINE UStaticMeshComponent* GetWeaponMesh() const { return WeaponMesh; }
	FORCEINLINE USceneComponent* GetTraceStartPoint() const { return TraceStartPoint; }
	FORCEINLINE USceneComponent* GetTraceEndPoint() const { return TraceEndPoint; }
	FORCEINLINE bool IsHitScanActive() const { return bIsHitScanActive; }
	
	// Network 등록 변수 설정
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
