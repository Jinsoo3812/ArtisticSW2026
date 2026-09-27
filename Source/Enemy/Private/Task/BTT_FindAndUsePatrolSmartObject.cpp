#include "Task/BTT_FindAndUsePatrolSmartObject.h"

#include "AI/AITask_UseGameplayBehaviorSmartObject.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "GameplayBehaviorSmartObjectBehaviorDefinition.h"
#include "GameplayTagAssetInterface.h"
#include "SmartObjectRequestTypes.h"
#include "SmartObjectSubsystem.h"

UBTT_FindAndUsePatrolSmartObject::UBTT_FindAndUsePatrolSmartObject()
{
	NodeName = TEXT("Find And Use Patrol Smart Object (No EQS)");
	bNotifyTaskFinished = true;

	HomeLocationKey.SelectedKeyName = TEXT("HomeLocation");
	HomeLocationKey.AddVectorFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTT_FindAndUsePatrolSmartObject, HomeLocationKey));

	PatrolRadiusKey.SelectedKeyName = TEXT("PatrolRadius");
	PatrolRadiusKey.AddFloatFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTT_FindAndUsePatrolSmartObject, PatrolRadiusKey));
}

void UBTT_FindAndUsePatrolSmartObject::InitializeMemory(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	EBTMemoryInit::Type InitType) const
{
	InitializeNodeMemory<FBTUsePatrolSmartObjectMemory>(NodeMemory, InitType);
}

void UBTT_FindAndUsePatrolSmartObject::CleanupMemory(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	EBTMemoryClear::Type CleanupType) const
{
	CleanupNodeMemory<FBTUsePatrolSmartObjectMemory>(NodeMemory, CleanupType);
}

EBTNodeResult::Type UBTT_FindAndUsePatrolSmartObject::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	USmartObjectSubsystem* SmartObjectSubsystem = OwnerComp.GetWorld()
		? USmartObjectSubsystem::GetCurrent(OwnerComp.GetWorld())
		: nullptr;
	if (!Controller || !Controller->HasAuthority() || !Blackboard || !Pawn || !SmartObjectSubsystem)
	{
		return EBTNodeResult::Failed;
	}

	FBTUsePatrolSmartObjectMemory* Memory =
		reinterpret_cast<FBTUsePatrolSmartObjectMemory*>(NodeMemory);
	Memory->TaskInstance.Reset();

	const FVector HomeLocation = Blackboard->GetValueAsVector(HomeLocationKey.SelectedKeyName);
	const float BlackboardRadius = Blackboard->GetValueAsFloat(PatrolRadiusKey.SelectedKeyName);
	const float SearchRadius = BlackboardRadius > 0.0f ? BlackboardRadius : FallbackRadius;

	FSmartObjectRequestFilter Filter;
	Filter.ActivityRequirements = ActivityRequirements;
	Filter.ClaimPriority = ClaimPriority;
	Filter.BehaviorDefinitionClasses = {
		UGameplayBehaviorSmartObjectBehaviorDefinition::StaticClass()
	};
	if (const IGameplayTagAssetInterface* TagSource =
		Cast<const IGameplayTagAssetInterface>(Pawn))
	{
		TagSource->GetOwnedGameplayTags(Filter.UserTags);
	}

	const FVector Extent(SearchRadius, SearchRadius, VerticalTolerance);
	const FSmartObjectRequest Request(FBox(HomeLocation - Extent, HomeLocation + Extent), Filter);
	TArray<FSmartObjectRequestResult> Results;
	const FSmartObjectActorUserData ActorUserData(Pawn);
	const FConstStructView UserData = FConstStructView::Make(ActorUserData);
	if (!SmartObjectSubsystem->FindSmartObjects(Request, Results, UserData))
	{
		return EBTNodeResult::Failed;
	}

	const int32 StartIndex = Results.Num() > 1
		? FMath::RandRange(0, Results.Num() - 1)
		: 0;
	for (int32 Offset = 0; Offset < Results.Num(); ++Offset)
	{
		const FSmartObjectRequestResult& Result = Results[(StartIndex + Offset) % Results.Num()];
		const TOptional<FVector> SlotLocation = SmartObjectSubsystem->GetSlotLocation(Result);
		if (!SlotLocation.IsSet()
			|| FVector::DistSquared2D(HomeLocation, SlotLocation.GetValue()) > FMath::Square(SearchRadius)
			|| FMath::Abs(HomeLocation.Z - SlotLocation.GetValue().Z) > VerticalTolerance)
		{
			continue;
		}

		const FSmartObjectClaimHandle ClaimHandle = SmartObjectSubsystem->MarkSlotAsClaimed(
			Result.SlotHandle,
			ClaimPriority,
			UserData);
		if (!ClaimHandle.IsValid())
		{
			continue;
		}

		UAITask_UseGameplayBehaviorSmartObject* UseTask =
			NewBTAITask<UAITask_UseGameplayBehaviorSmartObject>(OwnerComp);
		UseTask->SetClaimHandle(ClaimHandle);
		UseTask->SetShouldReachSlotLocation(true);
		UseTask->ReadyForActivation();
		Memory->TaskInstance = UseTask;
		return EBTNodeResult::InProgress;
	}

	return EBTNodeResult::Failed;
}

EBTNodeResult::Type UBTT_FindAndUsePatrolSmartObject::AbortTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	FBTUsePatrolSmartObjectMemory* Memory =
		reinterpret_cast<FBTUsePatrolSmartObjectMemory*>(NodeMemory);
	if (UAITask_UseGameplayBehaviorSmartObject* UseTask = Memory->TaskInstance.Get())
	{
		UseTask->ExternalCancel();
		Memory->TaskInstance.Reset();
	}

	return EBTNodeResult::Aborted;
}

FString UBTT_FindAndUsePatrolSmartObject::GetStaticDescription() const
{
	return ActivityRequirements.IsEmpty()
		? TEXT("Find and use a Gameplay Behavior Smart Object inside the patrol area without EQS")
		: FString::Printf(
			TEXT("Patrol Smart Object: %s (No EQS)"),
			*ActivityRequirements.GetDescription());
}
