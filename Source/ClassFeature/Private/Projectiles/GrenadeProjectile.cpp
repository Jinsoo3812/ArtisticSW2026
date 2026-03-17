#include "Projectiles/GrenadeProjectile.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Components/PrimitiveComponent.h"

AGrenadeProjectile::AGrenadeProjectile()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    SetReplicateMovement(true);

    // 메시 컴포넌트 생성 및 루트 등록 
    MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
    RootComponent = MeshComp;

    // 충돌 및 물리
    MeshComp->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    MeshComp->SetSimulatePhysics(false);
}

void AGrenadeProjectile::BeginPlay()
{
    Super::BeginPlay();

    // 서버에서만 폭발 타이머 작동
    if (HasAuthority())
    {
        FTimerHandle ExplodeTimerHandle;
        GetWorld()->GetTimerManager().SetTimer(ExplodeTimerHandle, this, &AGrenadeProjectile::Explode, ExplosionDelay, false);
    }
}

void AGrenadeProjectile::LaunchProjectile(const FVector& LaunchVelocity)
{
    // BP에서 설정된 RootComponent(물리가 켜진 StaticMesh 등)를 가져와 즉시 속도 적용
    if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(GetRootComponent()))
    {
        PrimComp->SetSimulatePhysics(true);
        PrimComp->SetPhysicsLinearVelocity(LaunchVelocity);
    }
}

// 지연 생성 단계에서 메시 에셋을 채워넣는 함수
void AGrenadeProjectile::SetGrenadeMesh(UStaticMesh* InMesh)
{
    if (MeshComp && InMesh)
    {
        MeshComp->SetStaticMesh(InMesh);
    }
}

void AGrenadeProjectile::Explode()
{
    TArray<AActor*> ActorsToIgnore;
    ActorsToIgnore.Add(this);
    TArray<AActor*> OverlappedActors;

    // 반경 내 캐릭터 스캔
    UKismetSystemLibrary::SphereOverlapActors(
        this,
        GetActorLocation(),
        ExplosionRadius,
        TArray<TEnumAsByte<EObjectTypeQuery>>(),
        ACharacter::StaticClass(),
        ActorsToIgnore,
        OverlappedActors
    );

    // 스캔된 캐릭터들에게 데미지(GE) 입히기
    if (DamageEffectSpecHandle.IsValid())
    {
        for (AActor* TargetActor : OverlappedActors)
        {
            if (UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor))
            {
                TargetASC->ApplyGameplayEffectSpecToSelf(*DamageEffectSpecHandle.Data.Get());
            }
        }
    }

    if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("Grenade Exploded!"));

    Destroy();
}