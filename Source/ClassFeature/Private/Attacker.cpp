// Source/ClassFeature/Private/Attacker.cpp

#include "Attacker.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Item/BaseItem.h" 
#include "AbilitySystemComponent.h"
#include "GASInputID.h"    

AAttacker::AAttacker()
{
    PrimaryActorTick.bCanEverTick = true;

    // ==========================================
    // [3인칭 캐릭터 회전 세팅 덮어쓰기]
    // ==========================================
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = false;
    bUseControllerRotationRoll = false;

    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

    // 조준 초기화
    bIsAiming = false;
    AimingSocketOffset = FVector(0.f, 70.f, 50.f);
    AimingTargetArmLength = 200.f;
    CameraInterpSpeed = 12.f;
}

void AAttacker::BeginPlay()
{
    Super::BeginPlay();

    // BasePlayer가 만들어둔 CameraBoom의 기본값을 저장
    if (CameraBoom)
    {
        DefaultSocketOffset = CameraBoom->SocketOffset;
        DefaultTargetArmLength = CameraBoom->TargetArmLength;
    }
}

void AAttacker::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 카메라 조준 줌인/줌아웃 보간
    if (CameraBoom)
    {
        FVector TargetOffset = bIsAiming ? AimingSocketOffset : DefaultSocketOffset;
        float TargetArmLength = bIsAiming ? AimingTargetArmLength : DefaultTargetArmLength;

        CameraBoom->SocketOffset = FMath::VInterpTo(CameraBoom->SocketOffset, TargetOffset, DeltaTime, CameraInterpSpeed);
        CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, TargetArmLength, DeltaTime, CameraInterpSpeed);
    }
}

void AAttacker::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    // 1. 부모(BasePlayer)의 이동, 시점, 점프, 줍기 바인딩을 그대로 가져옵니다.
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    // 2. Attacker 전용 Q키(UseSkillAction)만 추가로 선을 연결해 줍니다.
    if (UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(PlayerInputComponent))
    {
        if (UseSkillAction)
        {
            EnhancedInputComponent->BindAction(UseSkillAction, ETriggerEvent::Started, this, &AAttacker::UseSkillPressed);
            EnhancedInputComponent->BindAction(UseSkillAction, ETriggerEvent::Completed, this, &AAttacker::UseSkillReleased);
        }
    }
}

// ==========================================
// [스킬 및 타겟팅 로직 (유지)]
// ==========================================

void AAttacker::UseSkillPressed()
{

    // 정상 실행
    if (EquippedItem && AbilitySystemComponent)
    {
        bIsAiming = true;

        // ==========================================
        // 캐릭터 회전 방식 변경 (마우스 방향 바라보기)
        // ==========================================
        bUseControllerRotationYaw = true; // 컨트롤러(마우스)의 좌우 회전을 캐릭터에 적용
        GetCharacterMovement()->bOrientRotationToMovement = false; // 이동 방향으로 캐릭터가 자동으로 도는 기능 끄기


        AbilitySystemComponent->TryActivateAbilitiesByTag(FGameplayTagContainer(EquippedItem->ItemTag), true);
        AbilitySystemComponent->AbilityLocalInputPressed(static_cast<int32>(EGASInputID::UseSkill));

        if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green, TEXT("Success"));
    }
}

void AAttacker::UseSkillReleased()
{
    bIsAiming = false;

    // ==========================================
    // 캐릭터 회전 방식 원상복구 (원래 3인칭 상태)
    // ==========================================
    bUseControllerRotationYaw = false; // 컨트롤러 좌우 회전 적용 끄기
    GetCharacterMovement()->bOrientRotationToMovement = true; // 다시 이동하는 방향을 자연스럽게 쳐다보도록 켜기

    if (AbilitySystemComponent)
    {
        AbilitySystemComponent->AbilityLocalInputReleased(static_cast<int32>(EGASInputID::UseSkill));
    }
}

void AAttacker::ConsumeEquippedItem()
{
    if (EquippedItem)
    {
        EquippedItem->Destroy();
        EquippedItem = nullptr;
    }
}

bool AAttacker::TraceUnderCrosshairs(FHitResult& OutHitResult, float TraceDistance)
{
    if (FollowCamera == nullptr) return false;

    FVector Start = FollowCamera->GetComponentLocation();
    FVector End = Start + (FollowCamera->GetForwardVector() * TraceDistance);

    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(this);
    if (EquippedItem)
    {
        QueryParams.AddIgnoredActor(EquippedItem);
    }

    bool bHit = GetWorld()->LineTraceSingleByChannel(OutHitResult, Start, End, ECC_Visibility, QueryParams);

    if (!bHit)
    {
        OutHitResult.Location = End;
    }

    return true;
}