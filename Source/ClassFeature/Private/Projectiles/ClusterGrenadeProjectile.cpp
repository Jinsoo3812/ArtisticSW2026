#include "Projectiles/ClusterGrenadeProjectile.h"
#include "Projectiles/SubMunitionProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Math/UnrealMathUtility.h"
#include "Engine/World.h"
#include "TimerManager.h"

AClusterGrenadeProjectile::AClusterGrenadeProjectile()
{
	// 본체는 튕기지 않고 즉시 산탄되도록 함
	if (ProjectileMovement)
	{
		ProjectileMovement->bShouldBounce = false;
		ProjectileMovement->ProjectileGravityScale = 0.0f; // 직선으로 날아가도록 중력 제거
		ProjectileMovement->bRotationFollowsVelocity = true; // 날아가는 방향을 바라봄
	}

	if (MeshComp)
	{
		MeshComp->OnComponentHit.AddDynamic(this, &AClusterGrenadeProjectile::OnProjectileHit);
		// CollisionProfile 설정(Block 관련)은 Blueprint에서 기획자가 세팅하도록 함
	}
}

void AClusterGrenadeProjectile::BeginPlay()
{
	Super::BeginPlay();
	// Super::BeginPlay() 내부에서 ExplosionDelay(산탄 시간)를 이용해 Explode() 타이머가 설정됨.
}

void AClusterGrenadeProjectile::OnProjectileHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// 장애물 등 블록(Block) 충돌 발생 시 즉시 산탄
	if (HasAuthority())
	{
		Explode();
	}
}

void AClusterGrenadeProjectile::Explode()
{
	if (HasAuthority())
	{
		Split();
	}

	// 본체가 터질 때는 폭발 디버그(시각 효과)가 나타나지 않도록 주석 처리
	// Multicast_OnExploded();

	Destroy();
}

void AClusterGrenadeProjectile::Split()
{
	if (!SubMunitionClass) return;

	FVector CurrentLoc = GetActorLocation();
	FVector ForwardDir = GetVelocity().GetSafeNormal();
	if (ForwardDir.IsNearlyZero())
	{
		ForwardDir = GetActorForwardVector();
	}

	// 자탄들이 일제히 설치될 타임스탬프 계산
	float InstallTimestamp = GetWorld()->GetTimeSeconds() + SubMunitionInstallTime;

	TArray<ASubMunitionProjectile*> SpawnedSubMunitions;

	for (int32 i = 0; i < SubMunitionCount; ++i)
	{
		// 랜덤 원뿔(Cone) 방향 계산
		FVector RandDir = FMath::VRandCone(ForwardDir, FMath::DegreesToRadians(SpreadAngle));
		FVector LaunchVel = RandDir * SubMunitionLaunchSpeed;

		FTransform SpawnTransform(RandDir.Rotation(), CurrentLoc);
		
		// Deferred Spawn을 통해 변수(데미지스펙, 타임스탬프) 전달 후 생성 완료
		ASubMunitionProjectile* SubMunition = GetWorld()->SpawnActorDeferred<ASubMunitionProjectile>(
			SubMunitionClass, SpawnTransform, GetOwner(), GetInstigator(), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

		if (SubMunition)
		{
			SubMunition->DamageEffectSpecHandle = DamageEffectSpecHandle;
			SubMunition->InstallationTimestamp = InstallTimestamp;
			SubMunition->FinishSpawning(SpawnTransform);
			SubMunition->LaunchSubMunition(LaunchVel);
			SpawnedSubMunitions.Add(SubMunition);
		}
	}

	// 생성된 자탄끼리 서로의 충돌을 무시하도록 설정
	for (int32 i = 0; i < SpawnedSubMunitions.Num(); ++i)
	{
		for (int32 j = i + 1; j < SpawnedSubMunitions.Num(); ++j)
		{
			SpawnedSubMunitions[i]->MeshComp->IgnoreActorWhenMoving(SpawnedSubMunitions[j], true);
			SpawnedSubMunitions[j]->MeshComp->IgnoreActorWhenMoving(SpawnedSubMunitions[i], true);
		}
	}
}
