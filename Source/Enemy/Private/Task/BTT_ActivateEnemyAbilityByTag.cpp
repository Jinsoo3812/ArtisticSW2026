#include "Task/BTT_ActivateEnemyAbilityByTag.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogEnemyAbilityTask, Log, All);

UBTT_ActivateEnemyAbilityByTag::UBTT_ActivateEnemyAbilityByTag()
{
	NodeName = TEXT("Activate Enemy Ability By Tag");
	bCreateNodeInstance = true;
	bNotifyTaskFinished = true;
}

EBTNodeResult::Type UBTT_ActivateEnemyAbilityByTag::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	Cleanup();
	bAborting = false;
	bCompletionNotified = false;

	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	UAbilitySystemComponent* ASC = Pawn
		? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn)
		: nullptr;
	if (!Pawn || !Pawn->HasAuthority() || !ASC || !AbilityAssetTag.IsValid())
	{
		return EBTNodeResult::Failed;
	}

	CachedOwnerComp = &OwnerComp;
	CachedPawn = Pawn;
	CachedASC = ASC;
	if (!ValidateActivationContext(*Pawn, *ASC))
	{
		NotifyTaskFinishedOnce(EBTNodeResult::Failed);
		Cleanup();
		return EBTNodeResult::Failed;
	}

	const FGameplayAbilitySpec* Spec = FindAbilitySpec(*Pawn, *ASC);
	if (!Spec)
	{
		UE_LOG(LogEnemyAbilityTask, Warning,
			TEXT("No granted Enemy ability matched tag. Pawn=%s Tag=%s"),
			*GetNameSafe(Pawn), *AbilityAssetTag.ToString());
		NotifyTaskFinishedOnce(EBTNodeResult::Failed);
		Cleanup();
		return EBTNodeResult::Failed;
	}

	ActiveAbilityHandle = Spec->Handle;
	AbilityEndedDelegateHandle = ASC->OnAbilityEnded.AddUObject(
		this, &UBTT_ActivateEnemyAbilityByTag::HandleAbilityEnded);

	bExecutingActivation = true;
	const bool bActivated = ASC->TryActivateAbility(ActiveAbilityHandle, false);
	bExecutingActivation = false;
	if (!bActivated)
	{
		UE_LOG(LogEnemyAbilityTask, Verbose,
			TEXT("Enemy ability activation rejected. Pawn=%s Tag=%s Ability=%s"),
			*GetNameSafe(Pawn), *AbilityAssetTag.ToString(), *GetNameSafe(Spec->Ability));
		NotifyTaskFinishedOnce(EBTNodeResult::Failed);
		Cleanup();
		return EBTNodeResult::Failed;
	}

	if (bEndedDuringActivation)
	{
		const EBTNodeResult::Type Result = bEndedDuringActivationCancelled
			? EBTNodeResult::Failed
			: EBTNodeResult::Succeeded;
		NotifyTaskFinishedOnce(Result);
		Cleanup();
		return Result;
	}

	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type UBTT_ActivateEnemyAbilityByTag::AbortTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	bAborting = true;
	UAbilitySystemComponent* ASC = CachedASC.Get();
	const FGameplayAbilitySpecHandle HandleToCancel = ActiveAbilityHandle;
	const FGameplayAbilitySpec* ActiveSpec = ASC && HandleToCancel.IsValid()
		? ASC->FindAbilitySpecFromHandle(HandleToCancel)
		: nullptr;
	if (ASC && HandleToCancel.IsValid() && ShouldCancelAbilityOnAbort(ActiveSpec))
	{
		ASC->CancelAbilityHandle(HandleToCancel);
	}

	NotifyTaskFinishedOnce(EBTNodeResult::Aborted);
	Cleanup();
	return EBTNodeResult::Aborted;
}

void UBTT_ActivateEnemyAbilityByTag::OnTaskFinished(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	EBTNodeResult::Type TaskResult)
{
	NotifyTaskFinishedOnce(TaskResult);
	Cleanup();
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

FString UBTT_ActivateEnemyAbilityByTag::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("Activate and await %s%s"),
		*AbilityAssetTag.ToString(),
		bCancelAbilityOnAbort ? TEXT(" (cancel on abort)") : TEXT(""));
}

const FGameplayAbilitySpec* UBTT_ActivateEnemyAbilityByTag::FindAbilitySpec(
	APawn& Pawn,
	const UAbilitySystemComponent& AbilitySystem) const
{
	for (const FGameplayAbilitySpec& Spec : AbilitySystem.GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetAssetTags().HasTagExact(AbilityAssetTag))
		{
			return &Spec;
		}
	}
	return nullptr;
}

bool UBTT_ActivateEnemyAbilityByTag::ValidateActivationContext(
	APawn& Pawn,
	const UAbilitySystemComponent& AbilitySystem) const
{
	return true;
}

bool UBTT_ActivateEnemyAbilityByTag::ShouldCancelAbilityOnAbort(
	const FGameplayAbilitySpec* ActiveSpec) const
{
	return bCancelAbilityOnAbort;
}

void UBTT_ActivateEnemyAbilityByTag::OnAbilityTaskFinished(EBTNodeResult::Type Result)
{
}

void UBTT_ActivateEnemyAbilityByTag::HandleAbilityEnded(const FAbilityEndedData& EndedData)
{
	if (EndedData.AbilitySpecHandle != ActiveAbilityHandle || bAborting)
	{
		return;
	}

	if (bExecutingActivation)
	{
		bEndedDuringActivation = true;
		bEndedDuringActivationCancelled = EndedData.bWasCancelled;
		return;
	}

	FinishAbilityTask(EndedData.bWasCancelled
		? EBTNodeResult::Failed
		: EBTNodeResult::Succeeded);
}

void UBTT_ActivateEnemyAbilityByTag::FinishAbilityTask(EBTNodeResult::Type Result)
{
	UBehaviorTreeComponent* OwnerComp = CachedOwnerComp.Get();
	NotifyTaskFinishedOnce(Result);
	Cleanup();
	if (OwnerComp)
	{
		FinishLatentTask(*OwnerComp, Result);
	}
}

void UBTT_ActivateEnemyAbilityByTag::NotifyTaskFinishedOnce(EBTNodeResult::Type Result)
{
	if (!bCompletionNotified)
	{
		bCompletionNotified = true;
		OnAbilityTaskFinished(Result);
	}
}

void UBTT_ActivateEnemyAbilityByTag::Cleanup()
{
	if (UAbilitySystemComponent* ASC = CachedASC.Get(); ASC && AbilityEndedDelegateHandle.IsValid())
	{
		ASC->OnAbilityEnded.Remove(AbilityEndedDelegateHandle);
	}

	CachedOwnerComp.Reset();
	CachedPawn.Reset();
	CachedASC.Reset();
	ActiveAbilityHandle = FGameplayAbilitySpecHandle();
	AbilityEndedDelegateHandle.Reset();
	bExecutingActivation = false;
	bEndedDuringActivation = false;
	bEndedDuringActivationCancelled = false;
	bAborting = false;
}
