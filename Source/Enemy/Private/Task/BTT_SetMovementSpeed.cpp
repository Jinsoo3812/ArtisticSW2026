#include "Task/BTT_SetMovementSpeed.h"

#include "AIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UBTT_SetMovementSpeed::UBTT_SetMovementSpeed()
{
	NodeName = TEXT("Set Movement Speed");
}

EBTNodeResult::Type UBTT_SetMovementSpeed::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	const AAIController* AIController = OwnerComp.GetAIOwner();
	ACharacter* Character = AIController ? Cast<ACharacter>(AIController->GetPawn()) : nullptr;
	UCharacterMovementComponent* MovementComponent = Character ? Character->GetCharacterMovement() : nullptr;
	if (!MovementComponent)
	{
		return EBTNodeResult::Failed;
	}

	MovementComponent->MaxWalkSpeed = GetSpeedForMode(MovementMode);
	return EBTNodeResult::Succeeded;
}

FString UBTT_SetMovementSpeed::GetStaticDescription() const
{
	const UEnum* MovementModeEnum = StaticEnum<EEnemyMovementSpeedMode>();
	return FString::Printf(
		TEXT("Set Movement Speed: %s (%.0f cm/s)"),
		MovementModeEnum ? *MovementModeEnum->GetDisplayNameTextByValue(static_cast<int64>(MovementMode)).ToString() : TEXT("Unknown"),
		GetSpeedForMode(MovementMode));
}

float UBTT_SetMovementSpeed::GetSpeedForMode(EEnemyMovementSpeedMode Mode)
{
	switch (Mode)
	{
	case EEnemyMovementSpeedMode::Idle:
		return 0.0f;

	case EEnemyMovementSpeedMode::Jog:
		return 350.0f;

	case EEnemyMovementSpeedMode::Strafe:
		return 250.0f;

	case EEnemyMovementSpeedMode::Run:
		return 500.0f;

	default:
		return 0.0f;
	}
}
