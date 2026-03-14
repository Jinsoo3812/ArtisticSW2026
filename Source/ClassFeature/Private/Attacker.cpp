#include "Attacker.h"
#include "EnhancedInputComponent.h"
#include "Item/BaseItem.h" // 아이템 줍기 함수 호출용
#include "GASInputID.h"    // GAS 스킬 버튼 번호표
#include "AbilitySystemComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

AAttacker::AAttacker()
{
    PrimaryActorTick.bCanEverTick = true;

    // 틱 활성화 여부 등 기본 설정

    // ==========================================
    // [3인칭 캐릭터 회전 세팅]
    // ==========================================
    // 컨트롤러(마우스)가 회전할 때 캐릭터가 같이 휙휙 돌지 않게 막습니다. (카메라만 회전하도록)
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = false;
    bUseControllerRotationRoll = false;

    // 캐릭터가 이동하는 방향을 자연스럽게 쳐다보도록 설정합니다.
    GetCharacterMovement()->bOrientRotationToMovement = true; // 이 값이 3인칭 조작감의 핵심!
    GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f); // 회전 속도

    // ==========================================
    // [카메라 셀카봉 (Spring Arm) 세팅]
    // ==========================================
    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(RootComponent); // 캡슐 컴포넌트(루트)에 부착
    CameraBoom->TargetArmLength = 400.0f; // 캐릭터 등 뒤 400 거리에 카메라 위치
    CameraBoom->bUsePawnControlRotation = true; // 마우스 움직임에 따라 셀카봉이 회전하도록 설정

    // ==========================================
    // [실제 카메라 세팅]
    // ==========================================
    FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
    FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // 셀카봉 끝에 카메라 부착
    FollowCamera->bUsePawnControlRotation = false; // 카메라는 셀카봉을 따라다니기만 하면 되므로 회전 끔

    // 카메라 조준 연출 초기화
    bIsAiming = false;

    // 조준 시 목표 위치: Y(오른쪽)로 70, Z(위)로 50 만큼 비켜줍니다.
    AimingSocketOffset = FVector(0.f, 70.f, 50.f);

    // 조준 시 카메라 거리: 기본 400에서 200으로 가깝게 줌인!
    AimingTargetArmLength = 200.f;

    // 보간 속도: 10~15 정도면 꽤 부드럽고 빠릿합니다.
    CameraInterpSpeed = 12.f;
}

void AAttacker::BeginPlay()
{
    Super::BeginPlay();

    // 게임이 시작될 때 현재 카메라 셀카봉(SpringArm)의 세팅을 '기본값'으로 백업해둡니다.
    if (CameraBoom)
    {
        DefaultSocketOffset = CameraBoom->SocketOffset;
        DefaultTargetArmLength = CameraBoom->TargetArmLength;
    }
}

void AAttacker::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 카메라가 존재한다면 매 프레임 부드러운 이동 계산
    if (CameraBoom)
    {
        // 조준 중일 때의 목표값, 아닐 때의 목표값을 삼항 연산자로 결정
        FVector TargetOffset = bIsAiming ? AimingSocketOffset : DefaultSocketOffset;
        float TargetArmLength = bIsAiming ? AimingTargetArmLength : DefaultTargetArmLength;

        // VInterpTo (Vector Interpolation): 현재 위치에서 목표 위치로 스무스하게 이동
        CameraBoom->SocketOffset = FMath::VInterpTo(CameraBoom->SocketOffset, TargetOffset, DeltaTime, CameraInterpSpeed);

        // FInterpTo (Float Interpolation): 현재 거리에서 목표 거리로 스무스하게 줌인/줌아웃
        CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, TargetArmLength, DeltaTime, CameraInterpSpeed);
    }
}

void AAttacker::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    // Enhanced Input 시스템으로 변환
    if (UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(PlayerInputComponent))
    {
        // 1. F키 바인딩 (Started: 키를 누르는 순간 1번 발생)
        if (InteractAction)
        {
            EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &AAttacker::Interact);
        }

        // 2. Q키 바인딩 (Started: 누를 때 조준, Completed: 뗄 때 투척)
        if (UseSkillAction)
        {
            EnhancedInputComponent->BindAction(UseSkillAction, ETriggerEvent::Started, this, &AAttacker::UseSkillPressed);
            EnhancedInputComponent->BindAction(UseSkillAction, ETriggerEvent::Completed, this, &AAttacker::UseSkillReleased);
        }
        // 점프 (ACharacter에 내장된 Jump/StopJumping 함수 직접 연결) ---
        if (JumpAction)
        {
            EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
            EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
        }

        // 이동 (WASD) ---
        if (MoveAction)
        {
            EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AAttacker::Move);
        }

        // 시점 회전 (마우스) ---
        if (LookAction)
        {
            EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AAttacker::Look);
        }
    }
}





// ==========================================
// 추가: 기본 이동 및 회전 로직
// ==========================================
void AAttacker::Move(const FInputActionValue& Value)
{
    // 입력 값 (X, Y) 가져오기
    FVector2D MovementVector = Value.Get<FVector2D>();

    if (Controller != nullptr)
    {
        // 카메라가 바라보는 방향을 기준으로 '앞'과 '오른쪽'을 계산합니다.
        const FRotator Rotation = Controller->GetControlRotation();
        const FRotator YawRotation(0, Rotation.Yaw, 0);

        // 전진/후진 (W, S)
        const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
        // 좌/우 이동 (A, D)
        const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

        // 캐릭터 이동!
        AddMovementInput(ForwardDirection, MovementVector.Y);
        AddMovementInput(RightDirection, MovementVector.X);
    }
}

void AAttacker::Look(const FInputActionValue& Value)
{
    // 마우스 움직임 값 (X, Y) 가져오기
    FVector2D LookAxisVector = Value.Get<FVector2D>();

    if (Controller != nullptr)
    {
        // 컨트롤러(카메라) 회전
        AddControllerYawInput(LookAxisVector.X); // 좌우 회전
        AddControllerPitchInput(LookAxisVector.Y); // 상하 회전
    }
}


void AAttacker::Interact()
{
    // 이미 손에 무언가 들고 있다면 줍지 않음
    if (EquippedItem != nullptr) return;

    // 내 캐릭터 주변의 구체 범위 안에 있는 ABaseItem(아이템)들을 찾아서 배열에 담음
    TArray<AActor*> OverlappingActors;
    GetOverlappingActors(OverlappingActors, ABaseItem::StaticClass());

    // 겹친 아이템 중 첫 번째 것을 줍기
    for (AActor* Actor : OverlappingActors)
    {
        if (ABaseItem* Item = Cast<ABaseItem>(Actor))
        {
            Item->PickUpItem(this); // 아이템에 구현된 줍기 로직 실행!
            EquippedItem = Item;    // 손에 든 아이템으로 저장
            break;                  // 하나만 주우면 되므로 반복문 탈출
        }
    }
}

void AAttacker::UseSkillPressed()
{
    if (EquippedItem && AbilitySystemComponent)
    {

        // 카메라 조준 모드 ON!
        bIsAiming = true;

        // 1. 아이템이 가진 태그(Item.Weapon.Grenade 등)를 기반으로 내 몸 안의 스킬을 활성화 (조준 궤적 그리기 시작)
        AbilitySystemComponent->TryActivateAbilitiesByTag(FGameplayTagContainer(EquippedItem->ItemTag), true);

        // 2. GAS 시스템에게 "현재 3번(UseSkill) 키가 눌려있음!" 이라고 알려줌
        AbilitySystemComponent->AbilityLocalInputPressed(static_cast<int32>(EGASInputID::UseSkill));
    }
}

void AAttacker::UseSkillReleased()
{
    // 1. 카메라 조준 모드 OFF (스킬이 없는 빈손이어도 카메라는 돌아와야 하므로 무조건 끕니다)
    bIsAiming = false;

    if (AbilitySystemComponent)
    {

        // GAS 시스템에게 "누르고 있던 3번(UseSkill) 키를 뗐음!" 이라고 알려줌
        // -> 이 신호가 블루프린트 어빌리티의 'Wait Input Release' 노드를 발동시켜 수류탄이 날아감!
        AbilitySystemComponent->AbilityLocalInputReleased(static_cast<int32>(EGASInputID::UseSkill));
    }
}

void AAttacker::ConsumeEquippedItem()
{
    if (EquippedItem)
    {
        // 아이템 액터를 월드에서 삭제
        EquippedItem->Destroy();

        // 내 손을 다시 빈 손으로 만듦
        EquippedItem = nullptr;
    }
}


// ==========================================
// [멀티플레이 GAS 초기화]
// ==========================================

void AAttacker::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);

    // [서버 측 초기화]
    // BaseCharacter에 있는 AbilitySystemComponent가 유효한지 확인 후 초기화
    if (AbilitySystemComponent)
    {
        // InitAbilityActorInfo(OwnerActor, AvatarActor)
        // 공격자 본인이 스킬의 주인이자 실행자이므로 둘 다 this를 넣습니다.
        AbilitySystemComponent->InitAbilityActorInfo(this, this);
    }
}

void AAttacker::OnRep_PlayerState()
{
    Super::OnRep_PlayerState();

    // [클라이언트 측 초기화]
    // 멀티플레이 시, 클라이언트는 플레이어 정보가 서버로부터 넘어온 직후인 여기서 초기화합니다.
    if (AbilitySystemComponent)
    {
        AbilitySystemComponent->InitAbilityActorInfo(this, this);
    }
}

// ==========================================
// [조준 및 타겟팅 시스템]
// ==========================================
bool AAttacker::TraceUnderCrosshairs(FHitResult& OutHitResult, float TraceDistance)
{
    // 카메라가 없으면 작동하지 않음
    if (FollowCamera == nullptr) return false;

    // 1. 레이저의 시작점(Start)과 끝점(End) 계산
    // 시작점: 현재 카메라의 렌즈 위치
    FVector Start = FollowCamera->GetComponentLocation();

    // 끝점: 카메라가 바라보는 방향(앞)으로 TraceDistance(기본 100m)만큼 쭉 뻗은 곳
    FVector End = Start + (FollowCamera->GetForwardVector() * TraceDistance);

    // 2. 나 자신이나 내가 들고 있는 무기가 레이저에 맞는 것을 방지 (투명인간 취급)
    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(this);
    if (EquippedItem)
    {
        QueryParams.AddIgnoredActor(EquippedItem);
    }

    // 3. 월드에 레이저(Line Trace) 발사! (ECC_Visibility 채널 기준)
    bool bHit = GetWorld()->LineTraceSingleByChannel(OutHitResult, Start, End, ECC_Visibility, QueryParams);

    // 4. 만약 허공을 바라보고 있어서 아무것도 안 맞았다면?
    if (!bHit)
    {
        // 궤적이 엉뚱한 곳으로 튀지 않게, 끝점(100m 앞 허공)을 맞은 위치로 강제 설정해 줍니다.
        OutHitResult.Location = End;
    }

    // 나중에 에임에 이름표를 띄우고 싶다면, 여기서 맞은 액터(OutHitResult.GetActor())가 적인지 판별하면 됨
    return true;
}