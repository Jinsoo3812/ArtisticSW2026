#include "RangedEnemy/RangedEnemyAIController.h"

#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "RangedEnemy/RangedEnemy.h"
#include "TimerManager.h"

ARangedEnemyAIController::ARangedEnemyAIController()
{
	PrimaryActorTick.bCanEverTick = false;
	ConfigureRangedSight();

	if (UAIPerceptionComponent* RangedPerception = GetPerceptionComponent())
	{
		RangedPerception->OnTargetPerceptionUpdated.AddUniqueDynamic(
			this,
			&ARangedEnemyAIController::OnRangedTargetPerceptionUpdated);
	}
}

void ARangedEnemyAIController::OnPossess(APawn* PossessedPawn)
{
	Super::OnPossess(PossessedPawn);
	ConfigureRangedSight();

	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(
			CombatUpdateTimerHandle,
			this,
			&ARangedEnemyAIController::UpdateRangedCombat,
			FMath::Max(0.05f, CombatUpdateInterval),
			true,
			0.0f);
	}
}

void ARangedEnemyAIController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(CombatUpdateTimerHandle);
	ClearFocus(EAIFocusPriority::Gameplay);
	Super::OnUnPossess();
}

void ARangedEnemyAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(CombatUpdateTimerHandle);
	Super::EndPlay(EndPlayReason);
}

bool ARangedEnemyAIController::IsValidRangedTarget(const AActor* Candidate) const
{
	const ARangedEnemy* Enemy = Cast<ARangedEnemy>(GetPawn());
	return Enemy && Enemy->IsValidCombatTarget(Candidate);
}

void ARangedEnemyAIController::OnRangedTargetPerceptionUpdated(AActor* SeenTarget, FAIStimulus Stimulus)
{
	if (!HasAuthority())
	{
		return;
	}

	ARangedEnemy* Enemy = Cast<ARangedEnemy>(GetPawn());
	if (!Enemy)
	{
		return;
	}

	const bool bTargetValid = IsValidRangedTarget(SeenTarget);

	if (Stimulus.WasSuccessfullySensed() && bTargetValid)
	{
		Enemy->SetCombatTarget(SeenTarget);
		SetFocus(SeenTarget, EAIFocusPriority::Gameplay);
	}
	else if (Enemy->GetCombatTarget() == SeenTarget)
	{
		Enemy->ClearCombatTarget();
		ClearFocus(EAIFocusPriority::Gameplay);
	}

	UpdateRangedCombat();
}

void ARangedEnemyAIController::UpdateRangedCombat()
{
	if (!HasAuthority())
	{
		return;
	}

	ARangedEnemy* Enemy = Cast<ARangedEnemy>(GetPawn());
	if (!Enemy)
	{
		return;
	}

	AActor* Target = Enemy->GetCombatTarget();
	if (!IsValidRangedTarget(Target))
	{
		Target = SelectBestPerceivedTarget();
		Enemy->SetCombatTarget(Target);
		Target = Enemy->GetCombatTarget();
	}

	if (!Target)
	{
		ClearFocus(EAIFocusPriority::Gameplay);
		return;
	}

	SetFocus(Target, EAIFocusPriority::Gameplay);
	Enemy->TryStartRangedAttack();
}

AActor* ARangedEnemyAIController::SelectBestPerceivedTarget()
{
	const UAIPerceptionComponent* RangedPerception = GetPerceptionComponent();
	const ARangedEnemy* Enemy = Cast<ARangedEnemy>(GetPawn());
	if (!RangedPerception || !Enemy || !SightConfig)
	{
		return nullptr;
	}

	TArray<AActor*> PerceivedActors;
	RangedPerception->GetCurrentlyPerceivedActors(SightConfig->GetSenseImplementation(), PerceivedActors);

	AActor* BestTarget = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (AActor* Candidate : PerceivedActors)
	{
		if (!IsValidRangedTarget(Candidate))
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(Enemy->GetActorLocation(), Candidate->GetActorLocation());
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestTarget = Candidate;
		}
	}

	return BestTarget;
}

void ARangedEnemyAIController::ConfigureRangedSight()
{
	if (!SightConfig)
	{
		return;
	}

	SightConfig->SightRadius = FMath::Max(0.0f, RangedSightRadius);
	SightConfig->LoseSightRadius = FMath::Max(SightConfig->SightRadius, RangedLoseSightRadius);
	SightConfig->PeripheralVisionAngleDegrees = FMath::Clamp(RangedPeripheralVisionDegrees, 0.0f, 180.0f);
	SightConfig->SetMaxAge(FMath::Max(0.0f, StimulusMaxAgeSeconds));
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

	if (UAIPerceptionComponent* RangedPerception = GetPerceptionComponent())
	{
		RangedPerception->ConfigureSense(*SightConfig);
		RangedPerception->RequestStimuliListenerUpdate();
	}
}
