#include "GAS/Ability/Boss/GA_BossVanish.h"

#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "AI/PointSelectionFailure.h"
#include "AI/EnemyTerritoryComponent.h"
#include "BaseGameplayTags.h"
#include "BossAI/BossAttackPositionLibrary.h"
#include "BossAI/BossVanishFeedback.h"
#include "BossAI/ShipBossEnemy.h"
#include "GAS/Ability/Boss/BossGameplayAbility.h"
#include "GAS/Tasks/AbilityTask_BossVanishRelocation.h"
#include "ShipAI/EnemyShip.h"
#include "TimerManager.h"

namespace
{
	// Release only our own focus slot, leaving the AI/BT's normal focus intact.
	constexpr EAIFocusPriority::Type VanishFocusPriority = EAIFocusPriority::LastFocusPriority + 1;
}

UGA_BossVanish::UGA_BossVanish()
{
	SetVanishTags(GameplayAbility_Boss_Vanish, Cooldown_Boss_Vanish);
	ActivationBlockedTags.AddTag(State_Attacking);
	DepartureGameplayCueTag = GameplayCue_Boss_Vanish_Departure;
	ArrivalGameplayCueTag = GameplayCue_Boss_Vanish_Arrival;
}

UGA_BossVanishV2::UGA_BossVanishV2()
{
	SetVanishTags(GameplayAbility_Boss_VanishV2, Cooldown_Boss_VanishV2);
}

void UGA_BossVanish::SetVanishTags(FGameplayTag AbilityTag, FGameplayTag InCooldownTag)
{
	FGameplayTagContainer Tags(AbilityTag);
	Tags.AddTag(GameplayAbility_InterruptibleByHit);
	SetAssetTags(Tags);
	CooldownTag = InCooldownTag;
	VanishCooldownTags.Reset();
	VanishCooldownTags.AddTag(CooldownTag);
}

const FGameplayTagContainer* UGA_BossVanish::GetCooldownTags() const
{
	return &VanishCooldownTags;
}

void UGA_BossVanish::ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	// CommitAbility still checks cooldown/cost, but applying cooldown is deferred to EndAbility.
}

void UGA_BossVanish::ApplyVanishCooldown(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo) const
{
	if (UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr)
		UBossGameplayAbility::ApplyTaggedCooldown(*ASC, this, VanishCooldownTags, CooldownDuration, GetAbilityLevel(Handle, ActorInfo));
}

AShipBossEnemy* UGA_BossVanish::GetBossAvatar() const
{
	return Cast<AShipBossEnemy>(GetAvatarActorFromActorInfo());
}

void UGA_BossVanish::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bAttackMontageStarted = false;
	bConsumedVanish = false;
	AShipBossEnemy* Boss = ActorInfo ? Cast<AShipBossEnemy>(ActorInfo->AvatarActor.Get()) : nullptr;
	VanishTarget = Boss ? Boss->GetBossCombatTarget() : nullptr;
	if (Boss && Boss->HasAuthority() && VanishTarget.IsValid())
	{
		FocusController = Cast<AAIController>(Boss->GetController());
		if (FocusController.IsValid()) FocusController->SetFocus(VanishTarget.Get(), VanishFocusPriority);
	}
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (IsActive() && GetWorld())
		GetWorld()->GetTimerManager().SetTimer(TargetValidationTimer, this, &ThisClass::ValidateLockedTarget, 0.1f, true);
}

bool UGA_BossVanish::PlayAttackMontage(const FEnemyBasicAttackExecutionData& AttackData)
{
	// Attack data was cached and committed once. Keep the same busy state through relocation.
	if (!StartPreparedRelocation()) FinishAttack(true);
	return true;
}

bool UGA_BossVanish::StartPreparedRelocation()
{
	AShipBossEnemy* Boss = GetBossAvatar();
	FTransform Destination;
	if (!IsActive() || !Boss) return false;
	if (!IsVanishTargetValid()) { bConsumedVanish = true; return false; }
	if (!Boss->HasDestination() || !Boss->ResolveDestinationTransform(Destination))
	{
		EnemyPointSelectionFailure::Log(this, Boss, TEXT("No valid preselected Vanish relocation point."));
		return false;
	}
	RelocationTask = UAbilityTask_BossVanishRelocation::Relocate(
		this, Boss, GetVanishTarget(), PreparationMontage, PreparationDelay, HiddenDuration, RelocationSettleTime);
	if (!RelocationTask) return false;
	RelocationTask->OnRevealed.AddDynamic(this, &ThisClass::OnRelocationRevealed);
	RelocationTask->OnFailed.AddDynamic(this, &ThisClass::OnRelocationFailed);
	RelocationTask->OnDeparture.AddDynamic(this, &ThisClass::OnVanishDeparture);
	RelocationTask->OnArrival.AddDynamic(this, &ThisClass::OnVanishArrival);
	RelocationTask->ReadyForActivation();
	return true;
}

void UGA_BossVanish::OnRelocationRevealed()
{
	bConsumedVanish = true;
	RelocationTask = nullptr;
	if (IsActive()) HandleRevealedAtDestination();
}

void UGA_BossVanish::OnRelocationFailed()
{
	if (RelocationTask) bConsumedVanish |= RelocationTask->HasStartedHiding();
	if (!IsVanishTargetValid()) bConsumedVanish = true;
	if (IsActive()) FinishAttack(true);
}

bool UGA_BossVanish::IsVanishTargetValid() const
{
	const AShipBossEnemy* Boss = GetBossAvatar();
	AActor* Target = GetVanishTarget();
	if (!Boss || !IsValid(Target) || Target->IsActorBeingDestroyed() || !IsValid(Boss->GetHostShip())
		|| !Boss->CanEngageActor(Target)) return false;
	const UEnemyTerritoryComponent* Territory = Boss->GetTerritoryComponent();
	if (Territory && Territory->HasAssignedTerritory() && !Territory->IsInsideCombatArea(Target->GetActorLocation())) return false;
	// Being airborne is not target loss. Deck/surface availability remains the
	// destination selector and attack-position query's responsibility.
	return true;
}

void UGA_BossVanish::ValidateLockedTarget()
{
	if (!IsActive() || IsVanishTargetValid()) return;
	// Losing the locked player consumes the attempt even during preparation. Never retarget.
	bConsumedVanish = true;
	FinishAttack(true);
}

void UGA_BossVanish::OnVanishDeparture(FVector Location)
{
	if (IsActive()) BossVanishFeedback::ExecuteAtLocation(GetBossAvatar(), DepartureGameplayCueTag, Location);
}

void UGA_BossVanish::OnVanishArrival(FVector Location)
{
	if (IsActive()) BossVanishFeedback::ExecuteAtLocation(GetBossAvatar(), ArrivalGameplayCueTag, Location);
}

bool UGA_BossVanish::CanAttackFromCurrentPosition() const
{
	const AShipBossEnemy* Boss = GetBossAvatar();
	return IsVanishTargetValid()
		&& UBossAttackPositionLibrary::CanMeleeAttackFromCurrentPosition(Boss, GetVanishTarget(), GetAttackRangeInset());
}

void UGA_BossVanish::HandleRevealedAtDestination()
{
	if (CanAttackFromCurrentPosition()) StartAttackAtDestination();
	else FinishAttack(true);
}

void UGA_BossVanish::StartAttackAtDestination()
{
	bAttackMontageStarted = true;
	if (!Super::PlayAttackMontage(CachedExecutionData)) FinishAttack(true);
}

void UGA_BossVanish::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsActive()) return;
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(TargetValidationTimer);
	if (FocusController.IsValid()) FocusController->ClearFocus(VanishFocusPriority);
	FocusController.Reset();
	if (RelocationTask)
	{
		bConsumedVanish |= RelocationTask->HasStartedHiding();
		RelocationTask->EndTask();
		RelocationTask = nullptr;
	}
	// Normal completion is the attack's OnCompleted. Interrupted/exhausted consumed
	// attempts also start cooldown here, preventing immediate failed-relocation spam.
	if (bConsumedVanish)
	{
		bConsumedVanish = false;
		ApplyVanishCooldown(Handle, ActorInfo);
	}
	// Preserve the ordinary attack and variation cooldowns for the one actual swing.
	// They are never consumed by an intermediate relocation.
	if (bAttackMontageStarted) UGA_BossBasicAttack::ApplyCooldown(Handle, ActorInfo, ActivationInfo);
	if (AShipBossEnemy* Boss = GetBossAvatar()) Boss->ClearDestination();
	bAttackMontageStarted = false;
	VanishTarget.Reset();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
