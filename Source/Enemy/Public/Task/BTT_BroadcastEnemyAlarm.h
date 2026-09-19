#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTT_BroadcastEnemyAlarm.generated.h"

struct FBTEnemyAlarmMemory
{
	double LastBroadcastTime = -DBL_MAX;
};

/**
 * Reports a Hearing stimulus at the observing AI's position.
 * Nearby listeners investigate the alarm source through PointOfInterest and do
 * not receive the observed player's exact location or a combat target.
 */
UCLASS()
class ENEMY_API UBTT_BroadcastEnemyAlarm : public UBTTask_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTT_BroadcastEnemyAlarm();

	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
	virtual void InitializeMemory(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		EBTMemoryClear::Type CleanupType) const override;
	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FBTEnemyAlarmMemory); }

	virtual FString GetStaticDescription() const override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alarm", meta = (ClampMin = "0.0"))
	float Loudness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alarm", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxRange = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alarm")
	FName NoiseTag = TEXT("Enemy.Alarm.PlayerSighted");

	/** Prevents a looping combat subtree from generating a Hearing event every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alarm", meta = (ClampMin = "0.0", Units = "s"))
	float CooldownSeconds = 5.0f;
};
