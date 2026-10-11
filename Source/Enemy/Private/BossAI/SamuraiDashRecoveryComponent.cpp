#include "BossAI/SamuraiDashRecoveryComponent.h"

#include "AbilitySystemComponent.h"
#include "AI/BaseAIController.h"
#include "AI/EnemyBehaviorSet.h"
#include "BaseGameplayTags.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BossAI/BossDeckRecoveryComponent.h"
#include "BossAI/ShipBossEnemy.h"
#include "Components/BaseHealthComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckCombatTargetResolverComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ShipAI/EnemyShip.h"

DEFINE_LOG_CATEGORY_STATIC(LogSamuraiDashRecovery, Log, All);

USamuraiDashRecoveryComponent::USamuraiDashRecoveryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SamuraiCombatTree = TSoftObjectPtr<UBehaviorTree>(FSoftObjectPath(
		TEXT("/Game/GameplayAbilitySystem/Enemy/AI/SubTree/SamuraiBoss/BT_Subtree_SamuraiBoss_Combat.BT_Subtree_SamuraiBoss_Combat")));
}

AShipBossEnemy* USamuraiDashRecoveryComponent::GetBoss() const
{
	return Cast<AShipBossEnemy>(GetOwner());
}

bool USamuraiDashRecoveryComponent::IsEnabled() const
{
	const AShipBossEnemy* Boss = GetBoss();
	if (!Boss || !Boss->HasAuthority() || SamuraiCombatTree.IsNull()) return false;
	const UEnemyBehaviorSet* Set = Boss->GetBehaviorSet();
	const UBehaviorTree* Combat = Set ? Set->FindSubtree(EEnemyAIState::Combat) : nullptr;
	return (Combat && FSoftObjectPath(Combat) == SamuraiCombatTree.ToSoftObjectPath())
		|| (Boss->GetBehaviorTree() && FSoftObjectPath(Boss->GetBehaviorTree()) == SamuraiCombatTree.ToSoftObjectPath());
}

bool USamuraiDashRecoveryComponent::RefreshContext(AActor* Target)
{
	AShipBossEnemy* Boss = GetBoss();
	const ABaseAIController* AI = Boss ? Cast<ABaseAIController>(Boss->GetController()) : nullptr;
	if (!IsEnabled() || !AI || AI->GetEnemyState() != EEnemyAIState::Combat
		|| !Boss->GetHealthComponent() || Boss->GetHealthComponent()->IsDead() || !Boss->CanEngageActor(Target))
	{
		ResetPolicy();
		return false;
	}
	if (ObservedTarget.Get() != Target)
	{
		ResetPolicy();
		ObservedTarget = Target;
	}
	return true;
}

bool USamuraiDashRecoveryComponent::CanTryDash() const
{
	return IsEnabled() && ObservedTarget.IsValid() && !bNeedsReposition && !bRepositioning
		&& GetWorld() && GetWorld()->GetTimeSeconds() >= DashSelectionBlockedUntil;
}

bool USamuraiDashRecoveryComponent::CanStartReposition() const
{
	AShipBossEnemy* Boss = GetBoss();
	const UAbilitySystemComponent* ASC = Boss ? Boss->GetAbilitySystemComponent() : nullptr;
	return IsEnabled() && bNeedsReposition && !bRepositioning && Boss->CanEngageActor(ObservedTarget.Get())
		&& Boss->CanMoveOnDeck() && ASC && !ASC->HasMatchingGameplayTag(State_Boss_Busy);
}

bool USamuraiDashRecoveryComponent::RecordPointFailure(AActor* Target, const TCHAR* Reason)
{
	if (!RefreshContext(Target)) return false;
	AShipBossEnemy* Boss = GetBoss();
	const auto* Area = Boss->GetHostShip() ? Boss->GetHostShip()->GetDeckWalkAreaComponent() : nullptr;
	FDeckWalkLocation Start;
	FDeckTargetAnchor Anchor;
	// Missing support/readiness is handled by deck recovery, not tactical Dash failure counting.
	if (!Area || !Area->IsReady() || !Area->ResolveActorOnDeck(*Boss, Start)
		|| !UDeckCombatTargetResolverComponent::ResolveFor(Boss, Target, Anchor)
		|| !Boss->GetCharacterMovement()->IsMovingOnGround()
		|| (Boss->GetDeckRecoveryComponent() && Boss->GetDeckRecoveryComponent()->IsRecovering())
		|| bRepositioning || bNeedsReposition) return false;
	DashPointFailureCount = FMath::Min(DashPointFailureCount + 1, FMath::Max(1, MaximumPointFailures));
	bNeedsReposition = DashPointFailureCount >= FMath::Max(1, MaximumPointFailures);
	UE_LOG(LogSamuraiDashRecovery, Log, TEXT("Dash point failed. Boss=%s Count=%d/%d Reposition=%d Reason=%s"),
		*GetNameSafe(Boss), DashPointFailureCount, FMath::Max(1, MaximumPointFailures), bNeedsReposition, Reason);
	return true;
}

void USamuraiDashRecoveryComponent::RecordDashSuccess()
{
	if (IsEnabled() && !bRepositioning && !bNeedsReposition) DashPointFailureCount = 0;
}

bool USamuraiDashRecoveryComponent::BeginReposition()
{
	if (!CanStartReposition()) return false;
	bRepositioning = true;
	UE_LOG(LogSamuraiDashRecovery, Log, TEXT("Dash reposition started. Boss=%s"), *GetNameSafe(GetBoss()));
	return true;
}

void USamuraiDashRecoveryComponent::FinishReposition(bool bReached, bool bInterrupted)
{
	// Death/target changes can reset this policy before the old task finishes.
	if (!bRepositioning) return;
	bRepositioning = false;
	bNeedsReposition = false;
	DashPointFailureCount = 0;
	DashSelectionBlockedUntil = !bReached && !bInterrupted && GetWorld()
		? GetWorld()->GetTimeSeconds() + FMath::Max(0.1f, FailedMoveRetryDelay) : 0.0;
	UE_LOG(LogSamuraiDashRecovery, Log, TEXT("Dash reposition finished. Boss=%s Reached=%d Interrupted=%d"),
		*GetNameSafe(GetBoss()), bReached, bInterrupted);
}

void USamuraiDashRecoveryComponent::ResetPolicy()
{
	ObservedTarget.Reset();
	DashPointFailureCount = 0;
	bNeedsReposition = false;
	bRepositioning = false;
	DashSelectionBlockedUntil = 0.0;
}
