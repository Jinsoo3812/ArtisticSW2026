#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayEffectTypes.h" 
#include "GrenadeProjectile.generated.h"

class UProjectileMovementComponent;

UCLASS()
class CLASSFEATURE_API AGrenadeProjectile : public AActor
{
    GENERATED_BODY()

public:
    AGrenadeProjectile();

protected:
    virtual void BeginPlay() override;

public:
    // 물리 및 렌더링을 담당할 루트 메시 컴포넌트
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UStaticMeshComponent* MeshComp;

    // 투사체 움직임을 가볍게 연산하기 위한 컴포넌트
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UProjectileMovementComponent* ProjectileMovement;

    // GA가 수류탄을 생성할 때 넘겨줄 데미지 정보 (GE Spec)
    UPROPERTY(BlueprintReadWrite, meta = (ExposeOnSpawn = "true"), Category = "GAS")
    FGameplayEffectSpecHandle DamageEffectSpecHandle;

    // 몇 초 뒤에 터질 것인가?
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float ExplosionDelay = 3.0f;

    // 폭발 반경
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float ExplosionRadius = 300.f;

    // 폭발 시 이펙트 등을 모든 클라이언트에게 멀티캐스트
    UFUNCTION(NetMulticast, Reliable)
    void Multicast_OnExploded();

    // 실제 폭발 로직 (데미지 GE 적용)
    UFUNCTION()
    void Explode();

    // 생성 직후 단순 물리 힘(초기 속도)을 가해 던지기 위한 함수
    void LaunchProjectile(const FVector& LaunchVelocity);

    // 동적으로 스태틱 메시를 교체하는 함수
    void SetGrenadeMesh(UStaticMesh* InMesh);
};