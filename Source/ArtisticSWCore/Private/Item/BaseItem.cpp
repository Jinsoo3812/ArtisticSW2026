#include "Item/BaseItem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Character.h"

ABaseItem::ABaseItem()
{
    PrimaryActorTick.bCanEverTick = true;

    // 1. 메쉬 컴포넌트 생성 및 물리 설정
    ItemMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ItemMesh"));
    RootComponent = ItemMesh;

    // 외형은 블루프린트에서 할당하도록 비워둡니다. 물리만 기본으로 켜둡니다.
    ItemMesh->SetSimulatePhysics(true);

    // 2. 상호작용 충돌체 생성
    InteractSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractSphere"));
    InteractSphere->SetupAttachment(RootComponent);
    InteractSphere->SetSphereRadius(150.f); // 기본 상호작용 범위 (BP에서 수정 가능)
    InteractSphere->SetCollisionProfileName(TEXT("OverlapAllDynamic"));

    bIsHovering = false;
}

void ABaseItem::BeginPlay()
{
    Super::BeginPlay();

    // 바닥에 굴러가다 멈췄을 때 상태 전환 이벤트 바인딩
    if (ItemMesh)
    {
        ItemMesh->OnComponentSleep.AddDynamic(this, &ABaseItem::OnMeshSleep);
    }
}

void ABaseItem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 둥둥 뜨며 회전하는 효과 (최적화를 위해 물리가 꺼진 후 실행됨)
    if (bIsHovering)
    {
        // 1. 제자리 회전
        AddActorLocalRotation(FRotator(0.f, 45.f * DeltaTime, 0.f));

        // 2. 위아래로 둥둥 뜨기 (Sine 곡선 활용)
        float NewZ = HoverBaseLoc.Z + (FMath::Sin(GetGameTimeSinceCreation() * 3.f) * 10.f);
        SetActorLocation(FVector(HoverBaseLoc.X, HoverBaseLoc.Y, NewZ));
    }
}

void ABaseItem::OnMeshSleep(UPrimitiveComponent* SleepingComponent, FName BoneName)
{
    // 물리 엔진이 완전히 멈췄을 때 (Sleep 상태 돌입 시)
    if (ItemMesh)
    {
        ItemMesh->SetSimulatePhysics(false); // 물리 연산을 꺼서 최적화
    }

    HoverBaseLoc = GetActorLocation(); // 멈춘 위치 기록
    bIsHovering = true;                // Tick에서 애니메이션 시작
}

void ABaseItem::PickUpItem(AActor* Picker)
{
    if (!Picker) return;

    // 1. 둥둥 뜨는 이펙트 정지 및 상호작용 충돌체 끄기
    bIsHovering = false;
    InteractSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ItemMesh->SetSimulatePhysics(false);
    ItemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // 2. Picker(플레이어)를 Character로 캐스팅해서 손 소켓에 Attach
    ACharacter* PlayerCharacter = Cast<ACharacter>(Picker);
    if (PlayerCharacter)
    {
        // "GripPoint"나 "hand_rSocket" 등 사용하는 마네킹 메쉬의 소켓 이름을 적어주세요.
        AttachToComponent(PlayerCharacter->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("GripPoint"));
    }

    GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green, TEXT("Item Get!"));

    // TODO 3: (나중에 GAS 연동 시) Picker의 ASC를 가져와 GrantedAbilityClass를 부여
}

void ABaseItem::UseSkill()
{
    // 장착된 아이템의 Q스킬 사용 로직 
    // 플레이어의 ASC에서 아이템 태그를 기반으로 스킬을 활성화하도록 명령
}