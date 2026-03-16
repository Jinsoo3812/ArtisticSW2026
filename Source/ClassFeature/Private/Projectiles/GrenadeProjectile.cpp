#include "Projectiles/GrenadeProjectile.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetSystemLibrary.h"

AGrenadeProjectile::AGrenadeProjectile()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    SetReplicateMovement(true);

    // 1. 충돌체 생성
    CollisionComp = CreateDefaultSubobject<USphereComponent>(TEXT("SphereComp"));

    // 생성 성공 여부 확인 후 설정 진행
    if (CollisionComp)
    {
        CollisionComp->InitSphereRadius(15.0f);
        CollisionComp->SetCollisionProfileName(TEXT("BlockAllDynamic"));

        // 지난번에 추가한 Pawn 충돌 무시 코드 (안전하게 감싸기)
        CollisionComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

        RootComponent = CollisionComp;
    }

    // 2. 외형 생성
    MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
    if (MeshComp)
    {
        MeshComp->SetupAttachment(RootComponent);
        MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // 3. 발사체 컴포넌트 생성
    ProjectileMovementComp = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovementComp"));
    if (ProjectileMovementComp)
    {
        ProjectileMovementComp->UpdatedComponent = CollisionComp;
        ProjectileMovementComp->InitialSpeed = 1500.f;
        ProjectileMovementComp->MaxSpeed = 3000.f;
        ProjectileMovementComp->bRotationFollowsVelocity = true;
        ProjectileMovementComp->bShouldBounce = true;
        ProjectileMovementComp->ProjectileGravityScale = 1.2f;
    }

    // 기본 폭발 세팅
    ExplosionDelay = 3.0f;
    ExplosionRadius = 500.f;
}

void AGrenadeProjectile::BeginPlay()
{
    Super::BeginPlay();

    // 서버에서만 폭발 타이머를 작동시킵니다. (클라이언트가 맘대로 터뜨리면 안 되니까요!)
    if (HasAuthority())
    {
        FTimerHandle ExplodeTimerHandle;
        GetWorld()->GetTimerManager().SetTimer(ExplodeTimerHandle, this, &AGrenadeProjectile::Explode, ExplosionDelay, false);
    }
}

void AGrenadeProjectile::Explode()
{
    // 1. 폭발 반경(ExplosionRadius) 내에 있는 모든 '캐릭터(ACharacter)'를 찾습니다.
    TArray<AActor*> ActorsToIgnore;
    ActorsToIgnore.Add(this); // 자기 자신은 제외
    TArray<AActor*> OverlappedActors;

    UKismetSystemLibrary::SphereOverlapActors(
        this,
        GetActorLocation(),
        ExplosionRadius,
        TArray<TEnumAsByte<EObjectTypeQuery>>(),
        ACharacter::StaticClass(), // 캐릭터만 걸러냄
        ActorsToIgnore,
        OverlappedActors
    );

    // 2. 찾아낸 적들에게 데미지(GE) 주기
    if (DamageEffectSpecHandle.IsValid())
    {
        for (AActor* TargetActor : OverlappedActors)
        {
            // 대상의 몸에서 스킬 시스템(ASC)을 찾습니다.
            UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
            if (TargetASC)
            {
                // GA가 쥐어줬던 데미지 보따리를 터뜨려서 대상에게 입힙니다!
                TargetASC->ApplyGameplayEffectSpecToSelf(*DamageEffectSpecHandle.Data.Get());
            }
        }
    }

    // TODO: 여기에 펑! 터지는 나이아가라 파티클 생성 함수를 넣으시면 됩니다.
    if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("Grenade Exploded!"));

    // 터졌으니 수류탄 객체를 월드에서 지웁니다.
    Destroy();
}