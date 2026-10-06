#pragma once
#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTT_BroadcastEnemyAlarm.generated.h"
class USoundBase;

/** One server hearing event at the observed Player snapshot per combat session. */
UCLASS()
class ENEMY_API UBTT_BroadcastEnemyAlarm : public UBTTask_BlackboardBase
{
	GENERATED_BODY()
public:
	UBTT_BroadcastEnemyAlarm();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;
protected:
	UPROPERTY(EditAnywhere, Category = "Alarm", meta = (ClampMin = "0.0"))
	float Loudness = 1.0f;
	UPROPERTY(EditAnywhere, Category = "Alarm", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxRange = 2000.0f;
	UPROPERTY(EditAnywhere, Category = "Alarm", meta = (ClampMin = "0.0", Units = "s"))
	float CooldownSeconds = 5.0f;
	UPROPERTY(EditAnywhere, Category = "Alarm")
	TObjectPtr<USoundBase> AlarmSound;
};
