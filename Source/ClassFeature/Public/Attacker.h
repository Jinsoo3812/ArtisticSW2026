#pragma once

#include "CoreMinimal.h"
#include "BaseCharacter.h" // GASCore 모듈의 BaseCharacter 상속
#include "InputActionValue.h"
#include "Attacker.generated.h"

class UInputAction;
class ABaseItem;
class USpringArmComponent;
class UCameraComponent;

UCLASS()
class CLASSFEATURE_API AAttacker : public ABaseCharacter
{
    GENERATED_BODY()

public:
    AAttacker();

    // 서버에서 폰에 빙의(Possess)될 때 ASC를 초기화합니다.
    virtual void PossessedBy(AController* NewController) override;

    // 클라이언트에서 PlayerState가 복제(Replicate) 완료되었을 때 ASC를 초기화합니다.
    virtual void OnRep_PlayerState() override;

    // 매 프레임마다 카메라를 부드럽게 이동시키기 위해 틱 함수 오버라이드
	// (현재는 Q 누르고 있는 동안 시야를 바꾸기 위한 작업만 할당한 상태)
    virtual void Tick(float DeltaTime) override;

protected:

    virtual void BeginPlay() override; // 원래 카메라 값을 저장하기 위해 필요

    // 키보드/마우스 입력 바인딩 설정 함수
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

    // ==========================================
    // [입력 액션 (블루프린트에서 할당)]
    // ==========================================
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* InteractAction; // F키 (줍기)

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* UseSkillAction; // Q키 (스킬 사용)

    // 추가: 기본 조작 액션
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* JumpAction;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* MoveAction;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* LookAction;



    // ==========================================
    // [입력 실행 함수]
    // ==========================================
    void Interact();         // F키 눌렀을 때
    void UseSkillPressed();  // Q키 눌렀을 때 (조준 시작)
    void UseSkillReleased(); // Q키 뗐을 때 (투척 발동)

    // ==========================================
    // [카메라 컴포넌트]
    // ==========================================

    // 캐릭터 등 뒤에서 카메라를 들고 있는 '셀카봉' 역할
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
    USpringArmComponent* CameraBoom;

    // 실제로 화면을 비추는 카메라
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
    UCameraComponent* FollowCamera;

    // 추가: 기본 조작 함수 (점프는 ACharacter에 내장되어 있어 선언 불필요)
    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);


    // ==========================================
    // [카메라 조준 연출 변수]
    // ==========================================

    // 현재 Q를 누르고 조준 중인지 확인하는 플래그
    bool bIsAiming;

    // 원래 카메라 세팅 기억용
    FVector DefaultSocketOffset;
    float DefaultTargetArmLength;

    // 조준 시 이동할 카메라 세팅 (블루프린트에서 입맛대로 수정 가능하게 노출)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Aim")
    FVector AimingSocketOffset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Aim")
    float AimingTargetArmLength;

    // 카메라 이동 속도 (숫자가 클수록 휙! 움직임)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Aim")
    float CameraInterpSpeed;



public:

    // ==========================================
    // [조준 및 타겟팅 시스템]
    // ==========================================

    // 화면 정중앙이 가리키는 곳을 레이저(LineTrace)로 쏴서 알아내는 함수
    // 블루프린트에서 언제든 꺼내 쓸 수 있도록 BlueprintCallable로 만듭니다.
    UFUNCTION(BlueprintCallable, Category = "Targeting")
    bool TraceUnderCrosshairs(FHitResult& OutHitResult, float TraceDistance = 10000.f);


    // 카메라 겟어(Getter) 함수 (필요할 때 외부에서 가져다 쓰기 위함)
    FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
    FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }



    // 현재 캐릭터가 손에 들고 있는 장착 아이템
    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Equipment")
    ABaseItem* EquippedItem;

    // 아이템을 던진 후 손에서 없애는(소모하는) 함수 (블루프린트 어빌리티에서 호출용)
    UFUNCTION(BlueprintCallable, Category = "Equipment")
    void ConsumeEquippedItem();
};