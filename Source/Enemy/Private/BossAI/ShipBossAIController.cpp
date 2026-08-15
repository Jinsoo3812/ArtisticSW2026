#include "BossAI/ShipBossAIController.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "BaseGameplayTags.h"
#include "BossAI/ShipBossEnemy.h"
#include "TimerManager.h"

AShipBossAIController::AShipBossAIController()
{
	PointSelectionSettings.MaximumRearDot = 0.0f;
	PointSelectionSettings.MinimumTravelDistance = 100.0f;
	PointSelectionSettings.DashHitCorridorRadius = 120.0f;
	PointSelectionSettings.MaximumDashDistance = 1200.0f;
}

void AShipBossAIController::OnPossess(APawn* PossessedPawn)
{
	Super::OnPossess(PossessedPawn);
	const AShipBossEnemy* Boss = Cast<AShipBossEnemy>(PossessedPawn);
	if (HasAuthority() && Boss && !Boss->GetBehaviorTree())
	{
		GetWorldTimerManager().SetTimer(
			DecisionTimerHandle,
			this,
			&AShipBossAIController::EvaluateBossCombat,
			FMath::Max(0.05f, DecisionInterval),
			true);
	}
}

void AShipBossAIController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(DecisionTimerHandle);
	Super::OnUnPossess();
}

void AShipBossAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(DecisionTimerHandle);
	Super::EndPlay(EndPlayReason);
}

void AShipBossAIController::EvaluateBossCombat()
{
	AShipBossEnemy* Boss = Cast<AShipBossEnemy>(GetPawn());
	AActor* Target = Boss ? Boss->GetBossCombatTarget() : nullptr;
	if (!Boss || !Boss->CanEngageActor(Target))
	{
		ClearFocus(EAIFocusPriority::Gameplay);
		return;
	}

	UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent();
	if (!ASC || ASC->HasMatchingGameplayTag(State_Dead)
		|| ASC->HasMatchingGameplayTag(State_Boss_Busy)
		|| ASC->HasMatchingGameplayTag(State_Attacking))
	{
		return;
	}

	SetFocus(Target, EAIFocusPriority::Gameplay);
	const float Distance = FVector::Dist2D(Boss->GetActorLocation(), Target->GetActorLocation());
	if (Distance <= CloseCombatDistance)
	{
		if (Distance <= KnockbackDistance
			&& TryActivateAbilityByTag(*Boss, GameplayAbility_Boss_Knockback, Cooldown_Boss_Knockback))
		{
			return;
		}
		TryActivateAbilityByTag(*Boss, GameplayAbility_BasicAttack, Cooldown_Enemy_BasicAttack);
		return;
	}

	if (Distance > FarCombatDistance)
	{
		if (SelectAndActivateMobility(
			*Boss, *Target, EBossDestinationPurpose::Vanish,
			GameplayAbility_Boss_Vanish, Cooldown_Boss_Vanish))
		{
			return;
		}
		SelectAndActivateMobility(
			*Boss, *Target, EBossDestinationPurpose::Dash,
			GameplayAbility_Boss_DashSlash, Cooldown_Boss_DashSlash);
		return;
	}

	if (!SelectAndActivateMobility(
		*Boss, *Target, EBossDestinationPurpose::Dash,
		GameplayAbility_Boss_DashSlash, Cooldown_Boss_DashSlash))
	{
		SelectAndActivateMobility(
			*Boss, *Target, EBossDestinationPurpose::Vanish,
			GameplayAbility_Boss_Vanish, Cooldown_Boss_Vanish);
	}
}

bool AShipBossAIController::TryActivateAbilityByTag(
	AShipBossEnemy& Boss,
	FGameplayTag AbilityTag,
	FGameplayTag CooldownTag)
{
	UAbilitySystemComponent* ASC = Boss.GetAbilitySystemComponent();
	if (!ASC || !AbilityTag.IsValid()
		|| (CooldownTag.IsValid() && ASC->HasMatchingGameplayTag(CooldownTag)))
	{
		return false;
	}

	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetAssetTags().HasTagExact(AbilityTag)
			&& ASC->TryActivateAbility(Spec.Handle, false))
		{
			return true;
		}
	}
	return false;
}

bool AShipBossAIController::SelectAndActivateMobility(
	AShipBossEnemy& Boss,
	AActor& Target,
	EBossDestinationPurpose Purpose,
	FGameplayTag AbilityTag,
	FGameplayTag CooldownTag)
{
	UAbilitySystemComponent* ASC = Boss.GetAbilitySystemComponent();
	if (!ASC || (CooldownTag.IsValid() && ASC->HasMatchingGameplayTag(CooldownTag)))
	{
		return false;
	}

	int32 PointId = INDEX_NONE;
	if (!UBossDeckPointSelector::SelectDestinationPoint(
		Boss.GetHostShip(), &Boss, &Target, Purpose, PointSelectionSettings, PointId))
	{
		return false;
	}

	Boss.SetDestinationPointId(PointId);
	if (TryActivateAbilityByTag(Boss, AbilityTag, CooldownTag))
	{
		return true;
	}
	Boss.SetDestinationPointId(INDEX_NONE);
	return false;
}
