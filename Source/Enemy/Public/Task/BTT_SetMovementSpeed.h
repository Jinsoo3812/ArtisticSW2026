#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"

#include "BTT_SetMovementSpeed.generated.h"

UENUM(BlueprintType)
enum class EEnemyMovementSpeedMode : uint8
{
	Idle UMETA(DisplayName = "Idle (0)"),
	Jog UMETA(DisplayName = "Jog (350)"),
	Strafe UMETA(DisplayName = "Strafe (250)"),
	Run UMETA(DisplayName = "Run (500)"),
};

/** Selects the base locomotion speed; ABaseEnemy resolves wave and GAS modifiers. */
UCLASS()
class ENEMY_API UBTT_SetMovementSpeed : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_SetMovementSpeed();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;
	static float GetSpeedForMode(EEnemyMovementSpeedMode Mode);
	EEnemyMovementSpeedMode GetMovementMode() const { return MovementMode; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	EEnemyMovementSpeedMode MovementMode = EEnemyMovementSpeedMode::Jog;
};
