// Fill out your copyright notice in the Description page of Project Settings.


#include "Item/BaseProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"

// Sets default values
ABaseProjectile::ABaseProjectile()
{
	// 투척물 자체의 Tick은 필요 없으므로 최적화를 위해 끕니다.
	PrimaryActorTick.bCanEverTick = false;

	// 네트워크 동기화 설정 (서버에서 소환하면 클라로 날아감)
	bReplicates = true;
	SetReplicateMovement(true);

	// 1. 루트 콜리전 생성 및 세팅
	CollisionComp = CreateDefaultSubobject<UBoxComponent>(TEXT("BoxComp"));
	CollisionComp->InitBoxExtent(FVector(15.f)); // 큐브 크기에 맞게 조절
	CollisionComp->SetCollisionProfileName(TEXT("Projectile"));
	RootComponent = CollisionComp;

	// 2. 메시 컴포넌트 부착
	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	MeshComp->SetupAttachment(RootComponent);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision); // 충돌은 루트에서만 처리

	// 3. 발사체 무브먼트 컴포넌트 생성 및 세팅
	ProjectileMovementComp = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileComp"));
	ProjectileMovementComp->UpdatedComponent = CollisionComp;
	ProjectileMovementComp->InitialSpeed = 0.f; // GA에서 덮어씌울 예정
	ProjectileMovementComp->MaxSpeed = 3000.f;
	ProjectileMovementComp->bRotationFollowsVelocity = true; // 날아가는 방향으로 회전
	ProjectileMovementComp->bShouldBounce = true; // 바닥에 튕기도록 설정 (원치 않으면 false)

}

