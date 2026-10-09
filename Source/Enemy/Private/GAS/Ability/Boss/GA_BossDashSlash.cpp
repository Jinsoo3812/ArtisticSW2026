#include "GAS/Ability/Boss/GA_BossDashSlash.h"
#include "GAS/Tasks/AbilityTask_BossSlashDashExecution.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemComponent.h"
#include "AI/PointSelectionFailure.h"
#include "AIController.h"
#include "Animation/AnimMontage.h"
#include "BaseGameplayTags.h"
#include "BossAI/ShipBossEnemy.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GASAttributeDamageGameplayEffect.h"
#include "GAS/SWCombatEffectContextLibrary.h"
#include "GameplayCue/PathCombatPresentationDataAsset.h"
#include "ShipAI/EnemyShip.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogBossDashSlash, Log, All);

UBossDashSlashTelegraphEffect::UBossDashSlashTelegraphEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	GameplayCues.Add(FGameplayEffectCue(
		GameplayCue_Path_Boss_DashSlash_Telegraph, 0.0f, 1.0f));
}

UBossDashSlashExecutionPathEffect::UBossDashSlashExecutionPathEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(1.5f);
	GameplayCues.Add(FGameplayEffectCue(
		GameplayCue_Path_Boss_DashSlash_Execution, 0.0f, 1.0f));
}

UGA_BossDashSlash::UGA_BossDashSlash()
{
	SetBossAbilityTags(GameplayAbility_Boss_DashSlash, Cooldown_Boss_DashSlash);
	CooldownDuration = 5.0f;
	ImpactGameplayCueTag = GameplayCue_Impact_Boss_DashSlash;
	ChargingGameplayCueTag = GameplayCue_Boss_Samurai_Charging;
}

void UGA_BossDashSlash::PostLoad()
{
	Super::PostLoad();

	// Preserve values serialized by the pre-MontageConfig BPGA_SlashDash class.
	if (!MontageConfig.Montage && DashMontage_DEPRECATED)
	{
		MontageConfig.Montage = DashMontage_DEPRECATED;
		MontageConfig.WindupEnterSectionName = WindupSectionName_DEPRECATED;
		MontageConfig.AttackSectionName = DashSlashSectionName_DEPRECATED;
		MontageConfig.TravelHoldSectionName = DashHoldSectionName_DEPRECATED;
		MontageConfig.RecoverySectionName = RecoverySectionName_DEPRECATED;
		MontageConfig.WindupHoldDuration = WindupDuration_DEPRECATED;
		MontageConfig.RecoveryTimeout = RecoveryTimeout_DEPRECATED;
	}
}

void UGA_BossDashSlash::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	AShipBossEnemy* Boss = GetBossAvatar();
	FString Error;
	if (!Boss || !Boss->HasAuthority() || !Boss->CanEngageActor(GetBossTarget())
		|| !UAbilityTask_BossSlashDashExecution::ValidateConfig(MontageConfig, -1.f, Error))
	{
		UE_LOG(LogBossDashSlash, Warning, TEXT("DashSlash activation rejected: %s"), *Error);
		HandleExecutionFailed();
		return;
	}
	if (!Boss->HasDestination())
	{
		EnemyPointSelectionFailure::Log(this, Boss, TEXT("DashSlash has no preselected destination."));
		HandleExecutionFailed();
		return;
	}
	// Validate the committed path before consuming the original single-dash cooldown.
	auto* Area = Boss->GetHostShip() ? Boss->GetHostShip()->GetDeckWalkAreaComponent() : nullptr;
	FDeckWalkLocation Start;
	FTransform Goal;
	const FVector Up = Boss->GetHostShip() && Boss->GetHostShip()->GetShipDeckMesh() ? Boss->GetHostShip()->GetShipDeckMesh()->GetUpVector() : FVector::UpVector;
	if (!Area || !Area->ResolveActorOnDeck(*Boss, Start) || !Area->IsSupportedSegment(Start, Boss->GetDestinationLocation())
		|| !Boss->ResolveDestinationTransform(Goal)
		|| FVector::VectorPlaneProject(Goal.GetLocation() - Boss->GetActorLocation(), Up).Size() < FMath::Max(1.f, MinimumDashDistance))
	{
		EnemyPointSelectionFailure::Log(this, Boss, TEXT("Preselected DashSlash point or path is unsuitable."));
		HandleExecutionFailed();
		return;
	}
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo) || !StartDashExecution()) HandleExecutionFailed();
}

bool UGA_BossDashSlash::StartDashExecution(float TotalWaitOverride, bool bSkipWindup, bool bSkipRecovery)
{
	AShipBossEnemy* Boss = GetBossAvatar();
	if (!IsActive() || !Boss || !Boss->HasAuthority() || ExecutionTask) return false;
	const float Coefficient = Boss->GetBalancedBossAttackCoefficient(AttackCoefficient, true);
	// Open a new damage window once per dash so later steps may hit again.
	if (!PrepareStrengthAttack(Coefficient)) return false;
	ExecutionTask = UAbilityTask_BossSlashDashExecution::Execute(this, Boss, MontageConfig,
		DashDuration, MinimumDashDistance, DashHitRadius, PathPresentation, ChargingGameplayCueTag, TotalWaitOverride,
		bSkipWindup, bSkipRecovery);
	if (!ExecutionTask) return false;
	ExecutionTask->OnCompleted.AddDynamic(this, &ThisClass::HandleExecutionCompleted);
	ExecutionTask->OnFailed.AddDynamic(this, &ThisClass::HandleExecutionFailed);
	ExecutionTask->OnHit.AddDynamic(this, &ThisClass::HandleExecutionHit);
	ExecutionTask->ReadyForActivation();
	return true;
}

void UGA_BossDashSlash::HandleExecutionCompleted()
{
	ExecutionTask = nullptr;
	if (IsActive()) EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGA_BossDashSlash::HandleExecutionFailed()
{
	ExecutionTask = nullptr;
	if (IsActive()) EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UGA_BossDashSlash::HandleExecutionHit(AActor* Target, const FHitResult& Hit)
{
	ApplyDamageToTarget(Target, &Hit);
}

void UGA_BossDashSlash::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsActive()) return;
	if (ExecutionTask)
	{
		auto* Task = ExecutionTask.Get();
		ExecutionTask = nullptr;
		Task->OnCompleted.RemoveAll(this);
		Task->OnFailed.RemoveAll(this);
		Task->OnHit.RemoveAll(this);
		Task->EndTask();
	}
	if (bWasCancelled) if (auto* Boss = GetBossAvatar()) Boss->ClearDestination();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
