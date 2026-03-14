#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GrenadeProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

UCLASS()
class ARTISTICSWCORE_API AGrenadeProjectile : public AActor
{
    GENERATED_BODY()

public:
    AGrenadeProjectile();

protected:
    virtual void BeginPlay() override;

    // ==========================================
    // [컴포넌트 선언]
    // ==========================================

    // 1. 충돌을 담당할 구체 (루트 컴포넌트)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USphereComponent* CollisionComp;

    // 2. 수류탄의 외형을 담당할 메쉬
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UStaticMeshComponent* MeshComp;

    // 3. 포물선 이동, 중력, 튕김 등을 자동 계산해주는 마법의 컴포넌트
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UProjectileMovementComponent* ProjectileMovementComp;

    // ==========================================
    // [수류탄 로직]
    // ==========================================

    // 수류탄이 터질 때까지 걸리는 시간 (예: 3초)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
    float ExplosionDelay;

    // 폭발 반경
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
    float ExplosionRadius;

    // 펑! 터지는 로직 (타이머에 의해 호출됨)
    UFUNCTION(BlueprintCallable, Category = "Combat")
    virtual void Explode();
};