#include "Item/GrenadeProjectile.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"

AGrenadeProjectile::AGrenadeProjectile()
{
    PrimaryActorTick.bCanEverTick = false; // 틱 연산 불필요 (최적화)

    // 1. 충돌체(Sphere) 생성 및 루트로 설정
    CollisionComp = CreateDefaultSubobject<USphereComponent>(TEXT("SphereComp"));
    CollisionComp->InitSphereRadius(15.0f);
    // 발사체는 주로 Block 판정을 통해 벽이나 바닥에 튕기게 합니다.
    CollisionComp->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    RootComponent = CollisionComp;

    // 2. 외형 메쉬 생성 및 부착
    MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
    MeshComp->SetupAttachment(RootComponent);
    // 메쉬 자체가 충돌을 판정하면 구체(CollisionComp)와 충돌이 꼬이므로 꺼줍니다.
    MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // 3. 발사체 이동 컴포넌트 세팅 (가장 중요)
    ProjectileMovementComp = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovementComp"));
    ProjectileMovementComp->UpdatedComponent = CollisionComp; // 이 구체를 기준으로 움직임

    ProjectileMovementComp->InitialSpeed = 1500.f; // 처음에 던져지는 속도
    ProjectileMovementComp->MaxSpeed = 3000.f;     // 최대 속도 제한
    ProjectileMovementComp->bRotationFollowsVelocity = true; // 날아가는 방향을 쳐다보며 회전
    ProjectileMovementComp->bShouldBounce = true;  // 바닥에 닿으면 튕길 것인가? (수류탄이니까 True!)
    ProjectileMovementComp->Bounciness = 0.4f;     // 튕기는 탄성 (1.0이면 탱탱볼, 0.4면 묵직한 수류탄)
    ProjectileMovementComp->ProjectileGravityScale = 1.2f; // 중력의 영향력 (살짝 무겁게 떨어지도록)

    // 기본 변수 세팅
    ExplosionDelay = 3.0f; // 3초 뒤에 터짐
    ExplosionRadius = 500.f; // 폭발 반경
}

void AGrenadeProjectile::BeginPlay()
{
    Super::BeginPlay();

    // 생성된 지 'ExplosionDelay(3초)'가 지나면 Explode 함수를 자동으로 실행하는 타이머 설정
    FTimerHandle ExplodeTimerHandle;
    GetWorld()->GetTimerManager().SetTimer(ExplodeTimerHandle, this, &AGrenadeProjectile::Explode, ExplosionDelay, false);
}

void AGrenadeProjectile::Explode()
{
    // 나중에 여기에 파티클(나이아가라) 생성, 사운드 재생, 데미지 처리(GAS) 로직이 들어갑니다.

    // 디버그용 출력
    GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("수류탄 폭발 펑!!!!"));

    // 수류탄 액터 파괴
    Destroy();
}