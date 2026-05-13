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

	// 본체에서 계산해서 넘겨주는 설치 예정 시간 (GetWorld()->GetTimeSeconds() 기준)
	UPROPERTY(BlueprintReadWrite, meta = (ExposeOnSpawn = "true"), Category = "Cluster")
	float InstallationTimestamp = 0.0f;

	// 적과 닿았을 때(폭발 시) 적용할 데미지 반경
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	float ExplosionRadius = 150.f;

	// 본체가 산탄 시 초기 속도를 전달
	void LaunchSubMunition(const FVector& LaunchVelocity);

protected:
	ESubMunitionState CurrentState;

	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void ExplodeAndDestroy();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_OnExploded();

	void TransitionToInstalled();
	
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SetInstalled();

	// 설치 상태 이후 일정 시간이 지나면 파괴될 수명(TTL)
	UPROPERTY(EditDefaultsOnly, Category = "Cluster")
	float InstalledLifeSpan = 10.0f;
};
