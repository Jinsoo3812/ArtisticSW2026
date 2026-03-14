#include "Item/BaseItem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "GASInputID.h" // GASCore 모듈의 Enum 사용
#include "BaseCharacter.h" // 플레이어의 ASC를 가져오기 위해 캐스팅용으로 사용
#include "AbilitySystemComponent.h"

ABaseItem::ABaseItem()
{
    PrimaryActorTick.bCanEverTick = true;

    // 메쉬 설정 (기본적으로 물리 연산을 켜서 땅에 굴러가게 함)
    ItemMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ItemMesh"));
    RootComponent = ItemMesh;
    ItemMesh->SetSimulatePhysics(true);
    ItemMesh->SetGenerateOverlapEvents(true); // 카오스 물리 웨이크 이벤트 활성화

    // 상호작용(줍기) 범위 설정
    InteractSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractSphere"));
    InteractSphere->SetupAttachment(RootComponent);
    InteractSphere->SetSphereRadius(150.f);
    InteractSphere->SetCollisionProfileName(TEXT("OverlapAllDynamic"));

    bIsHovering = false;
}

void ABaseItem::BeginPlay()
{
    Super::BeginPlay();

    // 메쉬가 바닥에 굴러가다 완전히 멈추면 OnMeshSleep 함수 실행되도록 바인딩
    if (ItemMesh)
    {
        ItemMesh->OnComponentSleep.AddDynamic(this, &ABaseItem::OnMeshSleep);
    }
}

void ABaseItem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 물리가 멈추면 실행되는 둥둥 뜨기 + 회전 연산
    if (bIsHovering)
    {
        // Y축(Yaw)을 기준으로 빙글빙글 회전
        AddActorLocalRotation(FRotator(0.f, 45.f * DeltaTime, 0.f));

        // Z축(위아래)으로 싸인 곡선을 그리며 둥둥 뜸
        float NewZ = HoverBaseLoc.Z + (FMath::Sin(GetGameTimeSinceCreation() * 3.f) * 10.f);
        SetActorLocation(FVector(HoverBaseLoc.X, HoverBaseLoc.Y, NewZ));
    }
}

void ABaseItem::OnMeshSleep(UPrimitiveComponent* SleepingComponent, FName BoneName)
{
    // 최적화를 위해 무거운 물리 연산을 끔
    if (ItemMesh)
    {
        ItemMesh->SetSimulatePhysics(false);
    }

    HoverBaseLoc = GetActorLocation(); // 멈춘 위치 기억
    bIsHovering = true;                // 둥둥 뜨기 시작
}

void ABaseItem::PickUpItem(AActor* Picker)
{
    if (!Picker) return;

    // 1. 둥둥 뜨는 연산 정지 및 충돌체 끄기
    bIsHovering = false;
    InteractSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ItemMesh->SetSimulatePhysics(false);
    ItemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // 2. Picker(플레이어)를 ABaseCharacter로 변환
    ABaseCharacter* BaseChar = Cast<ABaseCharacter>(Picker);
    if (BaseChar)
    {
        // 플레이어의 손 소켓(GripPoint)에 아이템 부착
        // (사용하시는 마네킹의 소켓 이름에 맞게 "GripPoint"를 변경하세요 예: "hand_rSocket")
        AttachToComponent(BaseChar->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("GripPoint"));

        // 3. GAS 시스템에 스킬 부여 (핵심)
        UAbilitySystemComponent* ASC = BaseChar->GetAbilitySystemComponent();
        if (ASC && GrantedAbilityClass)
        {
            // 이 아이템의 스킬을 'EGASInputID::UseSkill (3번 = Q키)'와 연결해서 플레이어에게 줌!
            FGameplayAbilitySpec Spec(GrantedAbilityClass, 1, static_cast<int32>(EGASInputID::UseSkill), this);
            GrantedAbilityHandle = ASC->GiveAbility(Spec);
        }
    }
}