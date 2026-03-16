#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayEffectTypes.h" // GAS 데미지 보따리(SpecHandle)를 담기 위해 필수!
#include "GrenadeProjectile.generated.h"

UCLASS()
class CLASSFEATURE_API AGrenadeProjectile : public AActor
{
    GENERATED_BODY()

public:
    AGrenadeProjectile();

protected:
    virtual void BeginPlay() override;

public:
    // =========================================================================
    // [핵심] GA가 수류탄을 생성할 때 넘겨줄 '데미지 정보 보따리'
    // ExposeOnSpawn을 통해 블루프린트에서 스폰할 때 핀으로 노출시킬 수도 있습니다.
    // =========================================================================
    UPROPERTY(BlueprintReadWrite, meta = (ExposeOnSpawn = "true"), Category = "GAS")
    FGameplayEffectSpecHandle DamageEffectSpecHandle;

    // ==========================================
    // 물리 및 충돌 컴포넌트
    // ==========================================
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    class USphereComponent* CollisionComp;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    class UStaticMeshComponent* MeshComp;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    class UProjectileMovementComponent* ProjectileMovementComp;

    // ==========================================
    // 수류탄 폭발 설정값
    // ==========================================
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float ExplosionDelay; // 몇 초 뒤에 터질 것인가?

    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float ExplosionRadius; // 폭발 반경 (기본 500)

    // 실제 폭발 로직을 담당할 함수
    UFUNCTION()
    void Explode();

};