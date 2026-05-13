#pragma once

#include "CoreMinimal.h"
#include "Attacker/GA_ThrowGrenade.h"
#include "GA_ThrowClusterGrenade.generated.h"

UCLASS()
class CLASSFEATURE_API UGA_ThrowClusterGrenade : public UGA_ThrowGrenade
{
	GENERATED_BODY()

protected:
	virtual void DrawTrajectory() override;

	// 산탄 분리까지 걸리는 예상 시간 (궤적 그릴 때 길이 제한용)
	UPROPERTY(EditDefaultsOnly, Category = "Throw")
	float PredictedSplitTime = 2.0f;
};
