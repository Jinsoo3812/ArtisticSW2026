#include "GAS/Ability/Boss/GA_BossDashSlash.h"

#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "BaseGameplayTags.h"
#include "BossAI/ShipBossEnemy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GASDamageInstantGameplayEffect.h"
#include "ShipAI/EnemyShip.h"
#include "TimerManager.h"

UGA_BossDashSlash::UGA_BossDashSlash()
{
	SetBossAbilityTags(GameplayAbility_Boss_DashSlash, Cooldown_Boss_DashSlash);
	CooldownDuration = 5.0f;
	DamageEffectClass = UGASDamageInstantGameplayEffect::StaticClass();
	PointSelectionSettings.MaximumRearDot = 0.0f;
	PointSelectionSettings.MinimumTravelDistance = 100.0f;
	PointSelectionSettings.DashHitCorridorRadius = DashHitRadius;
}

void UGA_BossDashSlash::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!ResolveDestination() || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		FinishDash(true);
		return;
	}

	if (AShipBossEnemy* Boss = GetBossAvatar())
	{
		if (DashMontage && Boss->GetMesh() && Boss->GetMesh()->GetAnimInstance())
		{
			Boss->GetMesh()->GetAnimInstance()->Montage_Play(DashMontage);
		}
	}

	if (WindupDuration <= 0.0f)
	{
		BeginDash();
		return;
	}
	WindupTask = UAbilityTask_WaitDelay::WaitDelay(this, WindupDuration);
	WindupTask->OnFinish.AddDynamic(this, &UGA_BossDashSlash::BeginDash);
	WindupTask->ReadyForActivation();
}

void UGA_BossDashSlash::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (AShipBossEnemy* Boss = GetBossAvatar())
	{
		Boss->GetWorldTimerManager().ClearTimer(DashTimerHandle);
		if (bWasCancelled)
		{
			Boss->SetDestinationPointId(INDEX_NONE);
		}
	}
	ClearDashState();
	WindupTask = nullptr;
	DashElapsed = 0.0f;
	bTargetHit = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_BossDashSlash::BeginDash()
{
	AShipBossEnemy* Boss = GetBossAvatar();
	AEnemyShip* HostShip = Boss ? Boss->GetHostShip() : nullptr;
	UStaticMeshComponent* DeckMesh = HostShip ? HostShip->GetShipDeckMesh() : nullptr;
	FTransform Destination;
	if (!Boss || !DeckMesh || !Boss->ResolvePointTransform(Boss->GetDestinationPointId(), Destination))
	{
		FinishDash(true);
		return;
	}

	const FTransform DeckTransform = DeckMesh->GetComponentTransform();
	DashStartLocal = DeckTransform.InverseTransformPosition(Boss->GetActorLocation());
	DashEndLocal = DeckTransform.InverseTransformPosition(Destination.GetLocation());
	PreviousWorldLocation = Boss->GetActorLocation();
	DashElapsed = 0.0f;
	bTargetHit = false;

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		DashStateHandle = ApplyTimedStateTag(*ASC, State_Boss_Dashing, DashDuration + 0.25f);
	}
	Boss->GetWorldTimerManager().SetTimer(
		DashTimerHandle,
		this,
		&UGA_BossDashSlash::TickDash,
		DashTickInterval,
		true);
}

void UGA_BossDashSlash::TickDash()
{
	AShipBossEnemy* Boss = GetBossAvatar();
	AEnemyShip* HostShip = Boss ? Boss->GetHostShip() : nullptr;
	UStaticMeshComponent* DeckMesh = HostShip ? HostShip->GetShipDeckMesh() : nullptr;
	if (!Boss || !DeckMesh)
	{
		FinishDash(true);
		return;
	}

	DashElapsed += DashTickInterval;
	const float Alpha = FMath::Clamp(DashElapsed / FMath::Max(0.05f, DashDuration), 0.0f, 1.0f);
	const FVector NewWorldLocation = DeckMesh->GetComponentTransform().TransformPosition(
		FMath::Lerp(DashStartLocal, DashEndLocal, Alpha));
	const FVector MoveDirection = (NewWorldLocation - PreviousWorldLocation).GetSafeNormal();
	const FQuat NewRotation = MoveDirection.IsNearlyZero()
		? Boss->GetActorQuat()
		: FRotationMatrix::MakeFromXZ(MoveDirection, DeckMesh->GetUpVector()).ToQuat();

	ApplyDashHit(PreviousWorldLocation, NewWorldLocation);
	Boss->SetActorLocationAndRotation(
		NewWorldLocation,
		NewRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	PreviousWorldLocation = NewWorldLocation;

	if (Alpha >= 1.0f - KINDA_SMALL_NUMBER)
	{
		Boss->GetWorldTimerManager().ClearTimer(DashTimerHandle);
		Boss->SetBase(DeckMesh);
		Boss->MarkDestinationReached();
		ClearDashState();
		FinishDash(false);
	}
}

void UGA_BossDashSlash::ApplyDashHit(const FVector& SegmentStart, const FVector& SegmentEnd)
{
	if (bTargetHit)
	{
		return;
	}
	AActor* Target = GetBossTarget();
	if (!Target || !UBossDeckPointSelector::DoesSegmentPassTarget(
		SegmentStart, SegmentEnd, Target->GetActorLocation(), DashHitRadius))
	{
		return;
	}
	bTargetHit = ApplyDamageToTarget(Target, DamageEffectClass, Damage);
}

bool UGA_BossDashSlash::ResolveDestination()
{
	AShipBossEnemy* Boss = GetBossAvatar();
	AActor* Target = GetBossTarget();
	if (!Boss || !Target || !Boss->GetHostShip())
	{
		return false;
	}

	PointSelectionSettings.DashHitCorridorRadius = DashHitRadius;
	int32 PointId = INDEX_NONE;
	if (!UBossDeckPointSelector::SelectDestinationPoint(
		Boss->GetHostShip(), Boss, Target, EBossDestinationPurpose::Dash,
		PointSelectionSettings, PointId))
	{
		Boss->SetDestinationPointId(INDEX_NONE);
		return false;
	}
	Boss->SetDestinationPointId(PointId);
	return true;
}

void UGA_BossDashSlash::FinishDash(bool bWasCancelled)
{
	if (IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bWasCancelled);
	}
}

void UGA_BossDashSlash::ClearDashState()
{
	if (DashStateHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->RemoveActiveGameplayEffect(DashStateHandle);
		}
		DashStateHandle.Invalidate();
	}
}
