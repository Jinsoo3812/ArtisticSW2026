#include "GAS/Tasks/AbilityTask_BossTargetSnapshot.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BossAI/ShipBossEnemy.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckCombatTargetResolverComponent.h"
#include "ShipAI/EnemyShip.h"
#include "TimerManager.h"

UAbilityTask_BossTargetSnapshot* UAbilityTask_BossTargetSnapshot::Track(
	UGameplayAbility* Owner, AShipBossEnemy* InBoss, AActor* InTarget)
{
	auto* Task = NewAbilityTask<UAbilityTask_BossTargetSnapshot>(Owner);
	Task->Boss = InBoss;
	Task->Target = InTarget;
	return Task;
}

void UAbilityTask_BossTargetSnapshot::Activate()
{
	if (!Boss.IsValid() || !Boss->HasAuthority() || !Target.IsValid() || !GetWorld()) { EndTask(); return; }
	SamplePose();
	TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target.Get());
	if (TargetASC.IsValid())
	{
		DeathDelegate = TargetASC->RegisterGameplayTagEvent(State_Dead, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &ThisClass::OnDeadTagChanged);
		if (TargetASC->HasMatchingGameplayTag(State_Dead)) Freeze();
	}
	Target->OnEndPlay.AddDynamic(this, &ThisClass::OnTargetEndPlay);
	if (!bFrozen) GetWorld()->GetTimerManager().SetTimer(RefreshTimer, this, &ThisClass::Refresh, 0.05f, true);
}

const FBossTargetSnapshot& UAbilityTask_BossTargetSnapshot::GetSnapshot()
{
	Refresh();
	return Snapshot;
}

void UAbilityTask_BossTargetSnapshot::SamplePose()
{
	AEnemyShip* Ship = Boss.IsValid() ? Boss->GetHostShip() : nullptr;
	const UDeckWalkAreaComponent* Area = IsValid(Ship) ? Ship->GetDeckWalkAreaComponent() : nullptr;
	const UStaticMeshComponent* Frame = IsValid(Ship) ? Ship->GetDeckMeshComplex() : nullptr;
	AActor* Actor = Target.Get();
	if (!Area || !Frame || !IsValid(Actor)) return;
	FDeckTargetAnchor Anchor;
	if (UDeckCombatTargetResolverComponent::ResolveFor(Boss.Get(), Actor, Anchor))
	{
		Snapshot.LocalFloor = Anchor.LocalCenter;
		Snapshot.SurfaceId = Anchor.SurfaceId;
	}
	else if (Snapshot.bValid && (Actor->IsActorBeingDestroyed()
		|| (TargetASC.IsValid() && TargetASC->HasMatchingGameplayTag(State_Dead))))
	{
		// Freeze the death pose using the established surface; dead actors cannot be acquired as targets.
		const FVector Feet = Area->ToLocal(Area->GetActorFeetWorld(*Actor));
		Snapshot.LocalFloor.X = Feet.X; Snapshot.LocalFloor.Y = Feet.Y;
	}
	else { Snapshot.bValid = false; return; }
	Snapshot.LocalForward = FVector::VectorPlaneProject(
		Frame->GetComponentTransform().InverseTransformVectorNoScale(Actor->GetActorForwardVector()), FVector::UpVector).GetSafeNormal();
	Snapshot.bValid = !Snapshot.LocalForward.IsNearlyZero() && !Snapshot.LocalFloor.ContainsNaN();
}

void UAbilityTask_BossTargetSnapshot::Refresh()
{
	if (bFrozen || !IsActive()) return;
	if (!Target.IsValid() || Target->IsActorBeingDestroyed()
		|| (TargetASC.IsValid() && TargetASC->HasMatchingGameplayTag(State_Dead))) { Freeze(); return; }
	SamplePose();
}

void UAbilityTask_BossTargetSnapshot::Freeze()
{
	if (bFrozen) return;
	// The death tag callback precedes later corpse movement; capture the death position once.
	SamplePose();
	bFrozen = true;
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RefreshTimer);
}

void UAbilityTask_BossTargetSnapshot::OnDeadTagChanged(FGameplayTag Tag, int32 NewCount)
{
	if (NewCount > 0) Freeze();
}

void UAbilityTask_BossTargetSnapshot::OnTargetEndPlay(AActor* Actor, EEndPlayReason::Type Reason)
{
	Freeze();
}

void UAbilityTask_BossTargetSnapshot::OnDestroy(bool bAbilityEnded)
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RefreshTimer);
	if (Target.IsValid()) Target->OnEndPlay.RemoveDynamic(this, &ThisClass::OnTargetEndPlay);
	if (TargetASC.IsValid() && DeathDelegate.IsValid())
		TargetASC->RegisterGameplayTagEvent(State_Dead, EGameplayTagEventType::NewOrRemoved).Remove(DeathDelegate);
	Target.Reset();
	TargetASC.Reset();
	Super::OnDestroy(bAbilityEnded);
}
