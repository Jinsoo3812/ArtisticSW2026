#include "AI/BaseAIController.h"

#include "AI/EnemyBehaviorSet.h"
#include "AISystem.h"
#include "BaseEnemy.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Components/BaseHealthComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionTypes.h"
#include "Perception/AISense.h"
#include "Perception/AISense_Damage.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Damage.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogEnemyAI, Log, All);

ABaseAIController::ABaseAIController()
{
	// AAIController updates its ControlRotation from the active focus in Tick().
	// Disabling the controller tick lets path following move the pawn, but prevents
	// SetFocus() from turning the controlled character toward its target.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	SetupPerceptionSystem();
}

void ABaseAIController::SetupPerceptionSystem()
{
	UAIPerceptionComponent* PerceptionComp =
		CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComponent"));
	// AI|Perception controller defaults are the single editor-facing source of truth.
	// The component remains visible for inspection, but inherited Blueprints must not
	// edit its generated sense configurations independently.
	PerceptionComp->bEditableWhenInherited = false;
	SetPerceptionComponent(*PerceptionComp);

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	DamageConfig = CreateDefaultSubobject<UAISenseConfig_Damage>(TEXT("DamageConfig"));

	RefreshPerceptionConfiguration();

	PerceptionComp->SetDominantSense(*SightConfig->GetSenseImplementation());
	PerceptionComp->OnTargetPerceptionUpdated.AddDynamic(
		this,
		&ABaseAIController::OnTargetPerceptionUpdated);
}

void ABaseAIController::RefreshPerceptionConfiguration()
{
	UAIPerceptionComponent* PerceptionComp = GetPerceptionComponent();
	if (!PerceptionComp || !SightConfig || !HearingConfig || !DamageConfig)
	{
		return;
	}

	SightConfig->SightRadius = FMath::Max(0.0f, SightRadius);
	SightConfig->LoseSightRadius = FMath::Max(SightConfig->SightRadius, LoseSightRadius);
	SightConfig->SetMaxAge(FMath::Max(0.0f, SightMaxAge));
	SightConfig->PeripheralVisionAngleDegrees = FMath::Clamp(PeripheralVisionDegrees, 0.0f, 180.0f);
	SightConfig->AutoSuccessRangeFromLastSeenLocation = FMath::Max(0.0f, AutoSuccessRangeFromLastSeenLocation);
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

	HearingConfig->HearingRange = FMath::Max(0.0f, HearingRange);
	HearingConfig->SetMaxAge(FMath::Max(0.0f, HearingMaxAge));
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = true;

	DamageConfig->SetMaxAge(FMath::Max(0.0f, DamageMaxAge));

	PerceptionComp->ConfigureSense(*SightConfig);
	PerceptionComp->ConfigureSense(*HearingConfig);
	PerceptionComp->ConfigureSense(*DamageConfig);
	PerceptionComp->RequestStimuliListenerUpdate();
}

void ABaseAIController::OnPossess(APawn* PossessedPawn)
{
	Super::OnPossess(PossessedPawn);
	RefreshPerceptionConfiguration();

	ABaseEnemy* PossessedEnemy = Cast<ABaseEnemy>(PossessedPawn);
	if (!PossessedEnemy)
	{
		return;
	}

	BindPossessedEnemyDeath(PossessedEnemy);

	UBehaviorTree* RootBehaviorTree = PossessedEnemy->GetBehaviorTree();
	if (!RootBehaviorTree || !RootBehaviorTree->BlackboardAsset)
	{
		UE_LOG(LogEnemyAI, Verbose, TEXT("%s has no root Behavior Tree; perception remains configured but no state tree will run."), *GetNameSafe(PossessedEnemy));
		return;
	}

	UBlackboardComponent* BlackboardComponent = nullptr;
	if (!UseBlackboard(RootBehaviorTree->BlackboardAsset, BlackboardComponent) || !BlackboardComponent)
	{
		UE_LOG(LogEnemyAI, Error, TEXT("Failed to initialize Blackboard for %s."), *GetNameSafe(PossessedEnemy));
		return;
	}

	Blackboard = BlackboardComponent;
	InitializeBlackboardValues(PossessedPawn);

	if (!RunBehaviorTree(RootBehaviorTree))
	{
		UE_LOG(LogEnemyAI, Error, TEXT("Failed to run root Behavior Tree %s for %s."), *GetNameSafe(RootBehaviorTree), *GetNameSafe(PossessedEnemy));
		return;
	}

	ApplyBehaviorSet(PossessedEnemy->GetBehaviorSet());
}

void ABaseAIController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(TargetReacquireTimerHandle);
	CachedTargetActor.Reset();
	UnbindPerceivedTargetDeath();
	UnbindPossessedEnemyDeath();
	Super::OnUnPossess();
}

void ABaseAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(TargetReacquireTimerHandle);
	CachedTargetActor.Reset();
	UnbindPerceivedTargetDeath();
	UnbindPossessedEnemyDeath();
	Super::EndPlay(EndPlayReason);
}

EEnemyAIState ABaseAIController::GetEnemyState() const
{
	const UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
	if (!BlackboardComponent || BlackboardComponent->GetKeyID(StateKeyName) == FBlackboard::InvalidKey)
	{
		return EEnemyAIState::Passive;
	}

	return static_cast<EEnemyAIState>(BlackboardComponent->GetValueAsEnum(StateKeyName));
}

bool ABaseAIController::SetEnemyState(EEnemyAIState NewState)
{
	UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
	if (!HasAuthority() || !BlackboardComponent || BlackboardComponent->GetKeyID(StateKeyName) == FBlackboard::InvalidKey)
	{
		return false;
	}

	BlackboardComponent->SetValueAsEnum(StateKeyName, static_cast<uint8>(NewState));
	if (NewState != EEnemyAIState::Combat)
	{
		// A sequence can be aborted before its Clear Focus task executes.
		ClearFocus(EAIFocusPriority::Gameplay);
	}

	return true;
}

bool ABaseAIController::SetCombatTarget(AActor* TargetActor)
{
	UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
	if (!HasAuthority() || !BlackboardComponent || !IsValidPerceptionTarget(TargetActor))
	{
		return false;
	}

	GetWorldTimerManager().ClearTimer(TargetReacquireTimerHandle);
	CachedTargetActor = TargetActor;
	BlackboardComponent->SetValueAsObject(TargetActorKeyName, TargetActor);
	BindPerceivedTargetDeath(TargetActor);
	SetEnemyState(EEnemyAIState::Combat);
	return true;
}

AActor* ABaseAIController::GetCombatTarget() const
{
	AActor* TargetActor = CachedTargetActor.Get();
	return IsValid(TargetActor) ? TargetActor : nullptr;
}

void ABaseAIController::SetEQSPreviewTarget(AActor* TargetActor)
{
	CachedTargetActor = IsValid(TargetActor) ? TargetActor : nullptr;
}

void ABaseAIController::ClearCombatTarget(bool bReturnToPassive)
{
	GetWorldTimerManager().ClearTimer(TargetReacquireTimerHandle);
	CachedTargetActor.Reset();

	UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
	if (BlackboardComponent)
	{
		BlackboardComponent->ClearValue(TargetActorKeyName);
	}

	UnbindPerceivedTargetDeath();

	if (bReturnToPassive && GetEnemyState() == EEnemyAIState::Combat)
	{
		SetEnemyState(EEnemyAIState::Passive);
	}
}

bool ABaseAIController::StartInvestigation(const FVector& PointOfInterest)
{
	UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
	if (!HasAuthority() || !BlackboardComponent || !FAISystem::IsValidLocation(PointOfInterest))
	{
		return false;
	}

	const EEnemyAIState CurrentState = GetEnemyState();
	if (CurrentState == EEnemyAIState::Combat
		|| CurrentState == EEnemyAIState::Frozen
		|| CurrentState == EEnemyAIState::Dead)
	{
		return false;
	}

	BlackboardComponent->SetValueAsVector(PointOfInterestKeyName, PointOfInterest);
	return SetEnemyState(EEnemyAIState::Investigating);
}

bool ABaseAIController::RefreshBehaviorRouting()
{
	if (!HasAuthority())
	{
		return false;
	}

	ABaseEnemy* PossessedEnemy = Cast<ABaseEnemy>(GetPawn());
	UBehaviorTreeComponent* BehaviorTreeComponent = Cast<UBehaviorTreeComponent>(GetBrainComponent());
	if (!PossessedEnemy || !BehaviorTreeComponent)
	{
		UE_LOG(LogEnemyAI, Warning,
			TEXT("Failed to refresh behavior routing. Pawn=%s Brain=%s"),
			*GetNameSafe(GetPawn()), *GetNameSafe(GetBrainComponent()));
		return false;
	}

	// RunBehaviorDynamic resets its runtime BehaviorAsset to the node's default
	// whenever a complete tree restart recreates node instances. Resume the
	// stopped pooled tree first, then inject the pawn-specific BehaviorSet again.
	BehaviorTreeComponent->RestartLogic();
	ApplyBehaviorSet(PossessedEnemy->GetBehaviorSet());
	BehaviorTreeComponent->RestartTree(EBTRestartMode::ForceReevaluateRootNode);

	UE_LOG(LogEnemyAI, Log,
		TEXT("Refreshed behavior routing. Pawn=%s BehaviorSet=%s RootTree=%s"),
		*GetNameSafe(PossessedEnemy),
		*GetNameSafe(PossessedEnemy->GetBehaviorSet()),
		*GetNameSafe(PossessedEnemy->GetBehaviorTree()));
	return true;
}

void ABaseAIController::OnTargetPerceptionUpdated(AActor* SensedActor, FAIStimulus Stimulus)
{
	if (!HasAuthority() || !IsValid(SensedActor) || SensedActor == GetPawn())
	{
		return;
	}

	if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>())
	{
		HandleSightStimulus(SensedActor, Stimulus);
	}
	else if (Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>())
	{
		HandleHearingStimulus(SensedActor, Stimulus);
	}
	else if (Stimulus.Type == UAISense::GetSenseID<UAISense_Damage>())
	{
		HandleDamageStimulus(SensedActor, Stimulus);
	}
}

bool ABaseAIController::IsValidPerceptionTarget(const AActor* Candidate) const
{
	const ABaseEnemy* PossessedEnemy = Cast<ABaseEnemy>(GetPawn());
	return PossessedEnemy && PossessedEnemy->CanEngageActor(const_cast<AActor*>(Candidate));
}

void ABaseAIController::HandleSightStimulus(AActor* SensedActor, const FAIStimulus& Stimulus)
{
	if (Stimulus.WasSuccessfullySensed())
	{
		// Keep a valid current target stable. With two players, perception update
		// ordering must not make the enemy switch targets every time either player
		// produces a new sight stimulus.
		if (!GetCombatTarget())
		{
			SetCombatTarget(SensedActor);
		}
		return;
	}

	UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
	if (!BlackboardComponent || BlackboardComponent->GetValueAsObject(TargetActorKeyName) != SensedActor)
	{
		return;
	}

	if (AActor* ReplacementTarget = SelectBestPerceivedTarget())
	{
		SetCombatTarget(ReplacementTarget);
	}
	else
	{
		ClearCombatTarget(true);
	}
}

void ABaseAIController::HandleHearingStimulus(AActor* SensedActor, const FAIStimulus& Stimulus)
{
	if (Stimulus.WasSuccessfullySensed())
	{
		StartInvestigation(Stimulus.StimulusLocation);
	}
}

void ABaseAIController::HandleDamageStimulus(AActor* SensedActor, const FAIStimulus& Stimulus)
{
	if (Stimulus.WasSuccessfullySensed() && !GetCombatTarget())
	{
		SetCombatTarget(SensedActor);
	}
}

void ABaseAIController::OnPerceivedTargetDeathStarted(UBaseHealthComponent* HealthComponent)
{
	if (!HasAuthority() || ObservedTargetHealthComponent.Get() != HealthComponent)
	{
		return;
	}

	UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
	AActor* DeadTarget = BlackboardComponent
		? Cast<AActor>(BlackboardComponent->GetValueAsObject(TargetActorKeyName))
		: nullptr;
	if (BlackboardComponent && IsValid(DeadTarget))
	{
		BlackboardComponent->SetValueAsVector(PointOfInterestKeyName, DeadTarget->GetActorLocation());
	}

	ClearCombatTarget(false);
	ClearFocus(EAIFocusPriority::Gameplay);

	GetWorldTimerManager().SetTimer(
		TargetReacquireTimerHandle,
		this,
		&ABaseAIController::TryReacquireCombatTarget,
		FMath::Max(0.01f, TargetReacquireDelay),
		false);
}

void ABaseAIController::OnPossessedEnemyDeathStarted(UBaseHealthComponent* HealthComponent)
{
	GetWorldTimerManager().ClearTimer(TargetReacquireTimerHandle);
	ClearCombatTarget(false);

	if (UBlackboardComponent* BlackboardComponent = GetBlackboardComponent())
	{
		BlackboardComponent->ClearValue(PointOfInterestKeyName);
	}

	SetEnemyState(EEnemyAIState::Dead);
	StopMovement();
}

void ABaseAIController::InitializeBlackboardValues(APawn* PossessedPawn)
{
	CachedTargetActor.Reset();

	UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
	if (!BlackboardComponent || !PossessedPawn)
	{
		return;
	}

	BlackboardComponent->ClearValue(TargetActorKeyName);
	BlackboardComponent->ClearValue(PointOfInterestKeyName);
	BlackboardComponent->SetValueAsVector(HomeLocationKeyName, PossessedPawn->GetActorLocation());
	BlackboardComponent->SetValueAsFloat(PatrolRadiusKeyName, DefaultPatrolRadius);
	SetEnemyState(EEnemyAIState::Passive);
}

void ABaseAIController::ApplyBehaviorSet(const UEnemyBehaviorSet* BehaviorSet)
{
	if (!BehaviorSet)
	{
		UE_LOG(LogEnemyAI, Verbose, TEXT("%s has no Behavior Set; static nodes in the root tree remain usable."), *GetNameSafe(GetPawn()));
		return;
	}

	UBehaviorTreeComponent* BehaviorTreeComponent = Cast<UBehaviorTreeComponent>(GetBrainComponent());
	if (!BehaviorTreeComponent)
	{
		return;
	}

	TSet<FGameplayTag> InjectedTags;
	TSet<EEnemyAIState> ConfiguredStates;
	for (const FEnemyStateBehavior& Entry : BehaviorSet->GetStateBehaviors())
	{
		if (!Entry.InjectionTag.IsValid() || !Entry.Subtree)
		{
			UE_LOG(LogEnemyAI, Warning, TEXT("Behavior Set %s has an incomplete entry for state %d."), *GetNameSafe(BehaviorSet), static_cast<uint8>(Entry.State));
			continue;
		}

		if (InjectedTags.Contains(Entry.InjectionTag) || ConfiguredStates.Contains(Entry.State))
		{
			UE_LOG(LogEnemyAI, Warning, TEXT("Behavior Set %s contains a duplicate state or injection tag: %s."), *GetNameSafe(BehaviorSet), *Entry.InjectionTag.ToString());
			continue;
		}

		BehaviorTreeComponent->SetDynamicSubtree(Entry.InjectionTag, Entry.Subtree);
		UE_LOG(LogEnemyAI, Log,
			TEXT("Injected dynamic subtree. Pawn=%s State=%d Tag=%s Subtree=%s"),
			*GetNameSafe(GetPawn()),
			static_cast<uint8>(Entry.State),
			*Entry.InjectionTag.ToString(),
			*GetNameSafe(Entry.Subtree));
		InjectedTags.Add(Entry.InjectionTag);
		ConfiguredStates.Add(Entry.State);
	}
}

AActor* ABaseAIController::SelectBestPerceivedTarget() const
{
	const UAIPerceptionComponent* PerceptionComp = GetAIPerceptionComponent();
	const APawn* ControlledPawn = GetPawn();
	if (!PerceptionComp || !ControlledPawn || !SightConfig)
	{
		return nullptr;
	}

	TArray<AActor*> PerceivedActors;
	PerceptionComp->GetCurrentlyPerceivedActors(SightConfig->GetSenseImplementation(), PerceivedActors);

	AActor* BestTarget = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (AActor* Candidate : PerceivedActors)
	{
		if (!IsValidPerceptionTarget(Candidate))
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(ControlledPawn->GetActorLocation(), Candidate->GetActorLocation());
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestTarget = Candidate;
		}
	}

	return BestTarget;
}

void ABaseAIController::TryReacquireCombatTarget()
{
	if (!HasAuthority() || GetEnemyState() != EEnemyAIState::Combat)
	{
		return;
	}

	UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
	if (!BlackboardComponent || BlackboardComponent->GetValueAsObject(TargetActorKeyName))
	{
		return;
	}

	if (AActor* ReplacementTarget = SelectBestPerceivedTarget())
	{
		SetCombatTarget(ReplacementTarget);
	}
}

void ABaseAIController::BindPerceivedTargetDeath(AActor* TargetActor)
{
	UBaseHealthComponent* HealthComponent = TargetActor ? TargetActor->FindComponentByClass<UBaseHealthComponent>() : nullptr;
	if (ObservedTargetHealthComponent.Get() == HealthComponent)
	{
		return;
	}

	UnbindPerceivedTargetDeath();
	if (!HealthComponent)
	{
		return;
	}

	ObservedTargetHealthComponent = HealthComponent;
	HealthComponent->OnDeathStarted.AddUniqueDynamic(this, &ABaseAIController::OnPerceivedTargetDeathStarted);
}

void ABaseAIController::UnbindPerceivedTargetDeath()
{
	if (UBaseHealthComponent* HealthComponent = ObservedTargetHealthComponent.Get())
	{
		HealthComponent->OnDeathStarted.RemoveDynamic(this, &ABaseAIController::OnPerceivedTargetDeathStarted);
	}

	ObservedTargetHealthComponent.Reset();
}

void ABaseAIController::BindPossessedEnemyDeath(ABaseEnemy* PossessedEnemy)
{
	UnbindPossessedEnemyDeath();
	if (!PossessedEnemy)
	{
		return;
	}

	UBaseHealthComponent* HealthComponent = PossessedEnemy->GetHealthComponent();
	if (!HealthComponent)
	{
		return;
	}

	PossessedEnemyHealthComponent = HealthComponent;
	HealthComponent->OnDeathStarted.AddUniqueDynamic(this, &ABaseAIController::OnPossessedEnemyDeathStarted);
}

void ABaseAIController::UnbindPossessedEnemyDeath()
{
	if (UBaseHealthComponent* HealthComponent = PossessedEnemyHealthComponent.Get())
	{
		HealthComponent->OnDeathStarted.RemoveDynamic(this, &ABaseAIController::OnPossessedEnemyDeathStarted);
	}

	PossessedEnemyHealthComponent.Reset();
}
