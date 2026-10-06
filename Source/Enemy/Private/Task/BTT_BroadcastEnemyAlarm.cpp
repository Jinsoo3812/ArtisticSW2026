#include "Task/BTT_BroadcastEnemyAlarm.h"
#include "AI/BaseAIController.h"
#include "AI/EnemyAlarmComponent.h"
#include "BehaviorTree/BlackboardComponent.h"

UBTT_BroadcastEnemyAlarm::UBTT_BroadcastEnemyAlarm()
{
	NodeName = TEXT("Broadcast Alarm At Player");
	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTT_BroadcastEnemyAlarm, BlackboardKey), AActor::StaticClass());
}
EBTNodeResult::Type UBTT_BroadcastEnemyAlarm::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ABaseAIController* AI = Cast<ABaseAIController>(OwnerComp.GetAIOwner());
	APawn* Pawn = AI ? AI->GetPawn() : nullptr;
	UEnemyAlarmComponent* Alarm = Pawn ? Pawn->FindComponentByClass<UEnemyAlarmComponent>() : nullptr;
	AActor* Target = OwnerComp.GetBlackboardComponent() ? Cast<AActor>(OwnerComp.GetBlackboardComponent()->GetValueAsObject(GetSelectedBlackboardKey())) : nullptr;
	return AI && AI->HasAuthority() && AI->GetEnemyState() == EEnemyAIState::Combat && Alarm
		&& Alarm->Broadcast(Target, MaxRange, Loudness, CooldownSeconds, AlarmSound) ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
}
FString UBTT_BroadcastEnemyAlarm::GetStaticDescription() const
{
	return FString::Printf(TEXT("Hearing at Player snapshot; investigate before sight acquisition. Once per Combat session. Range %.0f cm."), MaxRange);
}
