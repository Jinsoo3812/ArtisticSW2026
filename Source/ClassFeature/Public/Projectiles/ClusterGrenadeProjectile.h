#pragma once

#include "CoreMinimal.h"
#include "Projectiles/GrenadeProjectile.h"
#include "ClusterGrenadeProjectile.generated.h"

class ASubMunitionProjectile;

UCLASS()
class CLASSFEATURE_API AClusterGrenadeProjectile : public AGrenadeProjectile
{
	GENERATED_BODY()

public:
	AClusterGrenadeProjectile();

protected:
	virtual void BeginPlay() override;

	virtual void Explode() override;

	UFUNCTION()
	void OnProjectileHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	void Split();

protected:
	// 산탄으로 분리될 자탄 클래스 (확장성을 위해 노출)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cluster")
	TSubclassOf<ASubMunitionProjectile> SubMunitionClass;

	// 산탄 개수
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cluster")
	int32 SubMunitionCount = 5;

	// 산탄 시 퍼지는 원뿔(Cone) 각도 (도 단위)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cluster")
	float SpreadAngle = 45.0f;

	// 분리 시 자탄에 가해질 초기 속력
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cluster")
	float SubMunitionLaunchSpeed = 1000.0f;

	// 자탄들이 바닥에 닿고 지뢰로 설치될 때까지의 시간 (본체에서 일제히 부여)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cluster")
	float SubMunitionInstallTime = 3.0f;
};
