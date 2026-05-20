#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayEffectTypes.h"
#include "SubMunitionProjectile.generated.h"

class UProjectileMovementComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class ESubMunitionState : uint8
{
	Moving,
	Installed
};

UCLASS()
class CLASSFEATURE_API ASubMunitionProjectile : public AActor
{
	GENERATED_BODY()

public:
	ASubMunitionProjectile();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* MeshComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UProjectileMovementComponent* ProjectileMovement;

	// 본체에서 전달받는 데미지 스펙
	UPROPERTY(BlueprintReadWrite, meta = (ExposeOnSpawn = "true"), Category = "GAS")
	FGameplayEffectSpecHandle DamageEffectSpecHandle;

	// 적과 닿았을 때(폭발 시) 적용할 데미지 반경
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	float ExplosionRadius = 150.f;

	// 즉시 폭발 모드 (true면 어딘가에 닿자마자 폭발, false면 바닥에 닿으면 지뢰로 설치됨)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Cluster")
	bool bExplodeOnImpact = false;

	// 지뢰 모드일 때, 설치 가능한 최대 바닥 경사각 (도 단위)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Cluster|Trap", meta = (EditCondition = "!bExplodeOnImpact"))
	float MaxInstallSlopeAngle = 20.0f;

	// 본체가 산탄 시 초기 속도를 전달
	void LaunchSubMunition(const FVector& LaunchVelocity);

protected:
	ESubMunitionState CurrentState;

	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	void ExplodeAndDestroy();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_OnExploded();

	void TransitionToInstalled();
	
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SetInstalled();

	// 설치 상태 이후 일정 시간이 지나면 자폭할 수명(TTL)
	UPROPERTY(EditDefaultsOnly, Category = "Cluster|Trap", meta = (EditCondition = "!bExplodeOnImpact"))
	float InstalledLifeSpan = 10.0f;
};
