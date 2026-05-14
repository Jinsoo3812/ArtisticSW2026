#pragma once

#include "Attacker/GA_ThrowGrenade.h"
#include "CoreMinimal.h"
#include "GA_ThrowClusterGrenade.generated.h"

UCLASS()
class CLASSFEATURE_API UGA_ThrowClusterGrenade : public UGA_ThrowGrenade {
  GENERATED_BODY()

protected:
  virtual void DrawTrajectory() override;

  // 기존 로직을 오버라이드하여, 투사체 스폰 시 PredictedSplitTime을 전달합니다.
  virtual void OnThrowEventReceived(FGameplayEventData Payload) override;

  // 산탄 분리까지 걸리는 예상 시간 (실제 투사체의 분리 시간과 궤적 길이에 모두 사용됨)
  UPROPERTY(EditDefaultsOnly, Category = "Throw")
  float PredictedSplitTime = 2.0f;
};
