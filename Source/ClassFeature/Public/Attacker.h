// Source/ClassFeature/Public/Attacker.h

#pragma once

#include "CoreMinimal.h"
#include "BasePlayer.h" // 부모 클래스를 BasePlayer로 변경!
#include "Attacker.generated.h"

UCLASS()
class CLASSFEATURE_API AAttacker : public ABasePlayer
{
    GENERATED_BODY()

public:
    AAttacker();
    virtual void Tick(float DeltaTime) override;

protected:
    virtual void BeginPlay() override;

    // 부모의 입력을 상속받으면서 수류탄 입력만 추가할 함수
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

    // ==========================================
    // [Attacker 전용: 스킬 및 투척 시스템]
    // ==========================================

    // Q키 (부모에는 없으므로 여기에 선언)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    class UInputAction* UseSkillAction;

    void UseSkillPressed();
    void UseSkillReleased();

    UFUNCTION(BlueprintCallable, Category = "Item")
    void ConsumeEquippedItem();

    UFUNCTION(BlueprintCallable, Category = "Targeting")
    bool TraceUnderCrosshairs(FHitResult& OutHitResult, float TraceDistance = 10000.f);

    // ==========================================
    // [카메라 조준 연출 변수]
    // ==========================================
    bool bIsAiming;
};