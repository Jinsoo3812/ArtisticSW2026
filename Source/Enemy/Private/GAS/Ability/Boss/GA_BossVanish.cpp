#include "GAS/Ability/Boss/GA_BossVanish.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BossAI/ShipBossEnemy.h"
#include "Components/StaticMeshComponent.h"
#include "ShipAI/EnemyShip.h"

UGA_BossVanish::UGA_BossVanish()
{
	SetBossAbilityTags(GameplayAbility_Boss_Vanish, Cooldown_Boss_Vanish);
	CooldownDuration = 7.0f;
}

void UGA_BossVanish::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!ValidatePreselectedDestination() || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		FinishVanish(true);
		return;
	}

	if (PreparationMontage)
	{
		MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, TEXT("BossVanishPreparation"), PreparationMontage);
		if (MontageTask)
		{
			MontageTask->OnInterrupted.AddDynamic(this, &UGA_BossVanish::HandleMontageInterrupted);
			MontageTask->OnCancelled.AddDynamic(this, &UGA_BossVanish::HandleMontageInterrupted);
			MontageTask->ReadyForActivation();
		}
	}

	if (PreparationDelay <= 0.0f)
	{
		BeginHiddenPhase();
		return;
	}
	PreparationTask = UAbilityTask_WaitDelay::WaitDelay(this, PreparationDelay);
	PreparationTask->OnFinish.AddDynamic(this, &UGA_BossVanish::BeginHiddenPhase);
	PreparationTask->ReadyForActivation();
}

void UGA_BossVanish::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (bWasCancelled)
	{
		if (AShipBossEnemy* Boss = GetBossAvatar())
		{
			Boss->SetDestinationPointId(INDEX_NONE);
		}
	}
	ClearHiddenState();
	MontageTask = nullptr;
	PreparationTask = nullptr;
	HiddenTask = nullptr;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_BossVanish::BeginHiddenPhase()
{
	AShipBossEnemy* Boss = GetBossAvatar();
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!Boss || !ASC)
	{
		FinishVanish(true);
		return;
	}

	HiddenStateHandle = ApplyTimedStateTag(*ASC, State_Boss_Hidden, HiddenDuration + 0.25f);
	Boss->SetBossHidden(true);
	HiddenTask = UAbilityTask_WaitDelay::WaitDelay(this, FMath::Max(0.01f, HiddenDuration));
	HiddenTask->OnFinish.AddDynamic(this, &UGA_BossVanish::Reappear);
	HiddenTask->ReadyForActivation();
}

void UGA_BossVanish::Reappear()
{
	AShipBossEnemy* Boss = GetBossAvatar();
	FTransform Destination;
	if (!Boss || !Boss->ResolvePointTransform(Boss->GetDestinationPointId(), Destination))
	{
		FinishVanish(true);
		return;
	}

	FVector FacingDirection = GetBossTarget()
		? GetBossTarget()->GetActorLocation() - Destination.GetLocation()
		: Destination.GetRotation().GetForwardVector();
	const FVector DeckUp = Boss->GetHostShip() && Boss->GetHostShip()->GetShipDeckMesh()
		? Boss->GetHostShip()->GetShipDeckMesh()->GetUpVector().GetSafeNormal()
		: FVector::UpVector;
	FacingDirection = FVector::VectorPlaneProject(FacingDirection, DeckUp).GetSafeNormal();
	const FQuat FacingRotation = FacingDirection.IsNearlyZero()
		? Destination.GetRotation()
		: FRotationMatrix::MakeFromXZ(FacingDirection, DeckUp).ToQuat();

	Boss->SetActorLocationAndRotation(
		Destination.GetLocation(),
		FacingRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	if (Boss->GetHostShip() && Boss->GetHostShip()->GetShipDeckMesh())
	{
		Boss->SetBase(Boss->GetHostShip()->GetShipDeckMesh());
	}
	Boss->MarkDestinationReached();
	ClearHiddenState();
	FinishVanish(false);
}

void UGA_BossVanish::HandleMontageInterrupted()
{
	FinishVanish(true);
}

bool UGA_BossVanish::ValidatePreselectedDestination() const
{
	const AShipBossEnemy* Boss = GetBossAvatar();
	AActor* Target = GetBossTarget();
	FTransform Destination;
	if (!Boss || !Boss->CanEngageActor(Target) || !Boss->GetHostShip()
		|| Boss->GetDestinationPointId() == INDEX_NONE
		|| !Boss->ResolvePointTransform(Boss->GetDestinationPointId(), Destination))
	{
		return false;
	}
	return true;
}

void UGA_BossVanish::FinishVanish(bool bWasCancelled)
{
	if (IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bWasCancelled);
	}
}

void UGA_BossVanish::ClearHiddenState()
{
	if (AShipBossEnemy* Boss = GetBossAvatar())
	{
		Boss->SetBossHidden(false);
	}
	if (HiddenStateHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->RemoveActiveGameplayEffect(HiddenStateHandle);
		}
		HiddenStateHandle.Invalidate();
	}
}
