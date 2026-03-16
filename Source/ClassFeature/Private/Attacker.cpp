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
#include "BaseGameplayTags.h"


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

    // 1. 현재 조준 상태인지 확인 (C++ 네이티브 태그 사용)
    bool bIsAimingState = false;
    if (AbilitySystemComponent)
    {
        bIsAimingState = AbilitySystemComponent->HasMatchingGameplayTag(State_Aiming);
    }

    // 캐릭터 회전 설정
    bUseControllerRotationYaw = bIsAimingState;
    GetCharacterMovement()->bOrientRotationToMovement = !bIsAimingState;

    // ==========================================
    // 카메라 줌인/줌아웃 (수치 직접 입력)
    // ==========================================
    if (CameraBoom)
    {
        // 변수 이름 앞에 Local_ 을 붙여서 헤더 파일의 변수와 이름이 겹치지 않게 수정!
        float Local_DefaultArmLength = 400.0f;     // 평소 카메라 거리
        float Local_AimingArmLength = 150.0f;      // 조준 시 카메라 거리

        FVector Local_DefaultOffset = FVector(0.f, 0.f, 0.f);
        FVector Local_AimingOffset = FVector(0.f, 60.f, 50.f); // 우측 어깨 너머

        float CameraSpeed = 10.0f; // 카메라 이동 속도

        // 목표값 정하기
        float TargetArmLength = bIsAimingState ? Local_AimingArmLength : Local_DefaultArmLength;
        FVector TargetSocketOffset = bIsAimingState ? Local_AimingOffset : Local_DefaultOffset;

        // 부드럽게 이동 (보간)
        CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, TargetArmLength, DeltaTime, CameraSpeed);
        CameraBoom->SocketOffset = FMath::VInterpTo(CameraBoom->SocketOffset, TargetSocketOffset, DeltaTime, CameraSpeed);
    }

    // ==========================================
    // 포물선은 일단 '조준 중' + '수류탄 들고 있을 때'만!
    // ==========================================
    // BaseGameplayTags.h 에 정의한 Item_Weapon_Grenade 네이티브 태그를 사용해 검사합니다.
    if (bIsAimingState && EquippedItem && EquippedItem->ItemTag == Item_Weapon_Grenade)
    {
        // 포물선 시작 위치
        FVector StartLoc = GetActorLocation();
        if (GetMesh()) StartLoc = GetMesh()->GetSocketLocation(FName("hand_r"));

        FVector CamLoc;
        FRotator CamRot;
        GetController()->GetPlayerViewPoint(CamLoc, CamRot);
        FVector TraceEnd = CamLoc + (CamRot.Vector() * 10000.f);

        FHitResult HitResult;
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(this);

        TArray<AActor*> AttachedActors;
        GetAttachedActors(AttachedActors);
        Params.AddIgnoredActors(AttachedActors);

        GetWorld()->LineTraceSingleByChannel(HitResult, CamLoc, TraceEnd, ECC_Visibility, Params);
        FVector TargetLoc = HitResult.bBlockingHit ? HitResult.Location : TraceEnd;

        // 발사 속도 계산
        FVector LaunchDir = (TargetLoc - StartLoc).GetSafeNormal();
        LaunchDir.Z += 0.15f;
        FVector LaunchVelocity = LaunchDir.GetSafeNormal() * 1500.f;

        // 가상의 공 던지기 설정
        FPredictProjectilePathParams PredictParams(5.0f, StartLoc, LaunchVelocity, 3.0f, ECollisionChannel::ECC_Visibility, this);
        PredictParams.DrawDebugType = EDrawDebugTrace::ForOneFrame;
        PredictParams.DrawDebugTime = DeltaTime;
        PredictParams.bTraceWithCollision = true;
        PredictParams.ActorsToIgnore.Append(AttachedActors);

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

        // 2. 스킬 발동 및 "키 눌림" 신호 전달 (유저님 원래 코드 방식!)
        AbilitySystemComponent->TryActivateAbilitiesByTag(FGameplayTagContainer(EquippedItem->ItemTag), true);

        // 이 한 줄이 WaitInputRelease에게 "야, 버튼 눌렀다!" 라고 알려주는 핵심입니다.
        AbilitySystemComponent->AbilityLocalInputPressed(static_cast<int32>(EGASInputID::UseSkill));
    }
}

void AAttacker::UseSkillReleased()
{

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