#include "Task/BTT_BroadcastEnemyAlarm.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "Perception/AISense_Hearing.h"

UBTT_BroadcastEnemyAlarm::UBTT_BroadcastEnemyAlarm()
{
	NodeName = TEXT("Broadcast Enemy Alarm");
	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTT_BroadcastEnemyAlarm, BlackboardKey),
		AActor::StaticClass());
}

void UBTT_BroadcastEnemyAlarm::InitializeMemory(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	EBTMemoryInit::Type InitType) const
{
	InitializeNodeMemory<FBTEnemyAlarmMemory>(NodeMemory, InitType);
}

void UBTT_BroadcastEnemyAlarm::CleanupMemory(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	EBTMemoryClear::Type CleanupType) const
{
	CleanupNodeMemory<FBTEnemyAlarmMemory>(NodeMemory, CleanupType);
}

EBTNodeResult::Type UBTT_BroadcastEnemyAlarm::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	const AActor* ObservedActor = Blackboard
		? Cast<AActor>(Blackboard->GetValueAsObject(GetSelectedBlackboardKey()))
		: nullptr;

	if (!Controller || !Controller->HasAuthority() || !Pawn || !IsValid(ObservedActor))
	{
		return EBTNodeResult::Failed;
	}

	FBTEnemyAlarmMemory* Memory = reinterpret_cast<FBTEnemyAlarmMemory*>(NodeMemory);
	const double CurrentTime = Pawn->GetWorld()->GetTimeSeconds();
	if (CurrentTime - Memory->LastBroadcastTime < CooldownSeconds)
	{
		return EBTNodeResult::Succeeded;
	}

	UAISense_Hearing::ReportNoiseEvent(
		Pawn,
		Pawn->GetActorLocation(),
		FMath::Max(0.0f, Loudness),
		Pawn,
		FMath::Max(0.0f, MaxRange),
		NoiseTag);
	Memory->LastBroadcastTime = CurrentTime;

	return EBTNodeResult::Succeeded;
}

FString UBTT_BroadcastEnemyAlarm::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("If %s is valid, report alarm at self (range %.0f cm, cooldown %.1f s)"),
		*BlackboardKey.SelectedKeyName.ToString(),
		MaxRange,
		CooldownSeconds);
}
