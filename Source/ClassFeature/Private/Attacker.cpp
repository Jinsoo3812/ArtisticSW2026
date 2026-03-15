// Source/ClassFeature/Private/Attacker.cpp

#include "Attacker.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Item/BaseItem.h" 
#include "AbilitySystemComponent.h"
#include "GASInputID.h"    
#include "Kismet/GameplayStatics.h"
#include "AbilitySystemBlueprintLibrary.h"


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

    // 부울 변수 대신 ASC의 태그를 직접 확인합니다.
    UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(this);
    bool bIsAimingTagActive = ASC && ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(FName("State.Aiming")));

    // 카메라 조준 줌인/줌아웃 보간
    if (CameraBoom)
    {
        FVector TargetOffset = bIsAiming ? AimingSocketOffset : DefaultSocketOffset;
        float TargetArmLength = bIsAiming ? AimingTargetArmLength : DefaultTargetArmLength;

        CameraBoom->SocketOffset = FMath::VInterpTo(CameraBoom->SocketOffset, TargetOffset, DeltaTime, CameraInterpSpeed);
        CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, TargetArmLength, DeltaTime, CameraInterpSpeed);
    }

    // ==========================================
    // 조준 중일 때 포물선 궤적 그리기
    // ==========================================
    if (bIsAiming && EquippedItem)
    {
        // 수류탄이 생성될 대략적인 시작 위치
        FVector StartLoc = GetActorLocation() + (GetActorForwardVector() * 50.f) + FVector(0, 0, 50.f);

        // 카메라가 바라보는 곳(목표 지점) 계산
        FVector CamLoc;
        FRotator CamRot;
        GetController()->GetPlayerViewPoint(CamLoc, CamRot);
        FVector TraceEnd = CamLoc + (CamRot.Vector() * 10000.f);

        FHitResult HitResult;
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(this);
        GetWorld()->LineTraceSingleByChannel(HitResult, CamLoc, TraceEnd, ECC_Visibility, Params);
        FVector TargetLoc = HitResult.bBlockingHit ? HitResult.Location : TraceEnd;

        // 던질 방향과 힘(속도) 계산
        FVector LaunchDir = (TargetLoc - GetActorLocation()).GetSafeNormal();
        LaunchDir.Z += 0.15f; // 살짝 위로 던지게
        FVector LaunchVelocity = LaunchDir.GetSafeNormal() * 1500.f; // 1500은 ThrowForce

        // 언리얼 내장 포물선 예측 및 그리기 함수
        FPredictProjectilePathParams PredictParams(15.0f, StartLoc, LaunchVelocity, 3.0f, ECollisionChannel::ECC_Visibility, this);
        PredictParams.DrawDebugType = EDrawDebugTrace::ForOneFrame; // 매 프레임 초록/빨간 선으로 그려줌
        PredictParams.DrawDebugTime = DeltaTime;
        PredictParams.bTraceWithCollision = true;

        FPredictProjectilePathResult PredictResult;
        UGameplayStatics::PredictProjectilePath(this, PredictParams, PredictResult);
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

    if (EquippedItem && AbilitySystemComponent)
    {
        // 1. 카메라 조준 모드 ON 및 캐릭터 회전
        bIsAiming = true;
        bUseControllerRotationYaw = true;
        GetCharacterMovement()->bOrientRotationToMovement = false;

        // 2. 스킬 발동 및 "키 눌림" 신호 전달 (유저님 원래 코드 방식!)
        AbilitySystemComponent->TryActivateAbilitiesByTag(FGameplayTagContainer(EquippedItem->ItemTag), true);

        // 이 한 줄이 WaitInputRelease에게 "야, 버튼 눌렀다!" 라고 알려주는 핵심입니다.
        AbilitySystemComponent->AbilityLocalInputPressed(static_cast<int32>(EGASInputID::UseSkill));
    }
}

void AAttacker::UseSkillReleased()
{
    // 1. 카메라 조준 모드 OFF 및 캐릭터 회전 원상복구
    bIsAiming = false;
    bUseControllerRotationYaw = false;
    GetCharacterMovement()->bOrientRotationToMovement = true;

    if (AbilitySystemComponent)
    {
        // 2. "키 뗐음" 신호 전달 (유저님 원래 코드 방식!)
        // 이 함수가 호출되는 순간, GA 안에 멈춰있던 WaitInputRelease가 즉시 반응합니다!
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