#include "BossAI/BossEncounterComponent.h"

#include "BossAI/ShipBossEnemy.h"
#include "Components/BaseHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "Engine/World.h"
#include "Interactable/InteractableComponent.h"
#include "Net/UnrealNetwork.h"
#include "Ship.h"
#include "ShipAI/EnemyShip.h"
#include "Storage/StorageChest.h"

UBossEncounterComponent::UBossEncounterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UBossEncounterComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!bEncounterEnabled)
	{
		return;
	}
	if (AEnemyShip* HostShip = Cast<AEnemyShip>(GetOwner()))
	{
		HostShip->OnDestroyed.AddUniqueDynamic(this, &UBossEncounterComponent::HandleHostShipDestroyed);
	}
	if (!EnemyItemBox)
	{
		EnemyItemBox = ResolveConfiguredEnemyItemBox();
	}

	if (EncounterTrigger == EBossEncounterTrigger::ItemBoxInteraction)
	{
		BindItemBox();
		if (GetOwner()->HasAuthority() && EnemyItemBox)
		{
			EnemyItemBox->SetPhysicsAndBuoyancyEnabled(false);
			EnemyItemBox->SetLocked(true);
		}
	}
}

void UBossEncounterComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindItemBox();
	if (SpawnedBoss && SpawnedBoss->GetHealthComponent())
	{
		SpawnedBoss->GetHealthComponent()->OnDeathStarted.RemoveDynamic(
			this, &UBossEncounterComponent::HandleBossDeathStarted);
	}
	if (AEnemyShip* HostShip = Cast<AEnemyShip>(GetOwner()))
	{
		HostShip->OnDestroyed.RemoveDynamic(this, &UBossEncounterComponent::HandleHostShipDestroyed);
	}
	Super::EndPlay(EndPlayReason);
}

void UBossEncounterComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBossEncounterComponent, EncounterState);
	DOREPLIFETIME(UBossEncounterComponent, SpawnedBoss);
}

void UBossEncounterComponent::ConfigureEncounter(
	AStorageChest* InEnemyItemBox,
	TSubclassOf<AShipBossEnemy> InBossClass,
	int32 InBossSpawnPointId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || EncounterState != EBossEncounterState::Waiting)
	{
		return;
	}

	UnbindItemBox();
	bEncounterEnabled = true;
	EnemyItemBox = InEnemyItemBox ? InEnemyItemBox : ResolveConfiguredEnemyItemBox();
	BossClass = InBossClass;
	BossSpawnPointId = InBossSpawnPointId;
	if (EncounterTrigger == EBossEncounterTrigger::ItemBoxInteraction)
	{
		BindItemBox();
	}
	if (AEnemyShip* HostShip = Cast<AEnemyShip>(GetOwner()))
	{
		HostShip->OnDestroyed.AddUniqueDynamic(this, &UBossEncounterComponent::HandleHostShipDestroyed);
	}
	if (EnemyItemBox && EncounterTrigger == EBossEncounterTrigger::ItemBoxInteraction)
	{
		EnemyItemBox->SetPhysicsAndBuoyancyEnabled(false);
		EnemyItemBox->SetLocked(true);
	}
}

AStorageChest* UBossEncounterComponent::ResolveConfiguredEnemyItemBox() const
{
	UChildActorComponent* BoxComponent = Cast<UChildActorComponent>(
		EnemyItemBoxComponent.GetComponent(GetOwner()));
	if (!BoxComponent)
	{
		return nullptr;
	}
	return Cast<AStorageChest>(BoxComponent->GetChildActor());
}

void UBossEncounterComponent::HandleItemBoxInteracted(AActor* Interactor)
{
	if (EncounterTrigger != EBossEncounterTrigger::ItemBoxInteraction)
	{
		return;
	}
	TryStartEncounter(Interactor);
}

bool UBossEncounterComponent::NotifyPlayerShipSighted(AShip* SensedPlayerShip)
{
	if (EncounterTrigger != EBossEncounterTrigger::PlayerShipSight)
	{
		return false;
	}
	return TryStartEncounter(SensedPlayerShip);
}

bool UBossEncounterComponent::TryStartEncounter(AActor* TriggerActor)
{
	if (!bEncounterEnabled || !GetOwner() || !GetOwner()->HasAuthority()
		|| EncounterState != EBossEncounterState::Waiting
		|| !IsValid(TriggerActor))
	{
		return false;
	}
	if (!ResolveEncounterTarget(TriggerActor))
	{
		// Sight can arrive while the helm possession transition is still settling.
		// Remain Waiting so a later successful stimulus can retry safely.
		return false;
	}

	// Change state before spawning so simultaneous interactions cannot create two bosses.
	SetEncounterState(EBossEncounterState::Spawning);
	if (!SpawnBossFor(TriggerActor))
	{
		SetEncounterState(EBossEncounterState::Failed);
		if (EnemyItemBox)
		{
			EnemyItemBox->SetLocked(true);
		}
		return false;
	}
	return true;
}

void UBossEncounterComponent::HandleBossDeathStarted(UBaseHealthComponent* HealthComponent)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || EncounterState != EBossEncounterState::Active)
	{
		return;
	}
	SetEncounterState(EBossEncounterState::Defeated);
}

void UBossEncounterComponent::HandleHostShipDestroyed(AActor* DestroyedActor)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || DestroyedActor != GetOwner())
	{
		return;
	}
	if (EncounterState != EBossEncounterState::Defeated)
	{
		SetEncounterState(EBossEncounterState::Failed);
	}
	if (EnemyItemBox && !EnemyItemBox->IsActorBeingDestroyed())
	{
		EnemyItemBox->Destroy();
	}
}

void UBossEncounterComponent::OnRep_EncounterState(EBossEncounterState OldState)
{
	OnEncounterStateChanged.Broadcast(OldState, EncounterState);
}

bool UBossEncounterComponent::SpawnBossFor(AActor* Interactor)
{
	AEnemyShip* HostShip = Cast<AEnemyShip>(GetOwner());
	UWorld* World = GetWorld();
	AActor* CombatTarget = ResolveEncounterTarget(Interactor);
	if (!HostShip || !World || !BossClass || !CombatTarget)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BossEncounter] Spawn rejected. Host=%s BossClass=%s Target=%s"),
			*GetNameSafe(HostShip), *GetNameSafe(BossClass.Get()), *GetNameSafe(CombatTarget));
		return false;
	}

	int32 SpawnPointId = INDEX_NONE;
	FTransform SpawnTransform;
	if (!ResolveSpawnPoint(*HostShip, SpawnPointId, SpawnTransform))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BossEncounter] No valid boss spawn point. Ship=%s AuthoredPointId=%d"),
			*GetNameSafe(HostShip), BossSpawnPointId);
		return false;
	}

	AShipBossEnemy* Boss = World->SpawnActorDeferred<AShipBossEnemy>(
		BossClass,
		SpawnTransform,
		HostShip,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Boss)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossEncounter] Deferred boss spawn failed. Ship=%s"),
			*GetNameSafe(HostShip));
		return false;
	}

	Boss->FinishSpawning(SpawnTransform);
	if (!Boss->InitializeBoss(HostShip, SpawnPointId, CombatTarget))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BossEncounter] Boss initialization failed. Ship=%s PointId=%d Target=%s"),
			*GetNameSafe(HostShip), SpawnPointId, *GetNameSafe(CombatTarget));
		Boss->Destroy();
		return false;
	}

	SpawnedBoss = Boss;
	if (UBaseHealthComponent* Health = Boss->GetHealthComponent())
	{
		Health->OnDeathStarted.AddUniqueDynamic(this, &UBossEncounterComponent::HandleBossDeathStarted);
	}

	if (EnemyItemBox && EncounterTrigger == EBossEncounterTrigger::ItemBoxInteraction)
	{
		TArray<ABaseCharacter*> Guards;
		Guards.Add(Boss);
		EnemyItemBox->ConfigureGuarding(true, Guards, HostShip);
	}
	SetEncounterState(EBossEncounterState::Active);
	return true;
}

AActor* UBossEncounterComponent::ResolveEncounterTarget(AActor* TriggerActor) const
{
	if (AShip* TriggerShip = Cast<AShip>(TriggerActor))
	{
		return TriggerShip->GetRidingPlayer();
	}
	return TriggerActor;
}

bool UBossEncounterComponent::ResolveSpawnPoint(
	AEnemyShip& HostShip,
	int32& OutPointId,
	FTransform& OutTransform) const
{
	UDeckWaypointComponent* Point = Cast<UDeckWaypointComponent>(
		BossSpawnPointComponent.GetComponent(&HostShip));
	OutPointId = Point ? Point->GetWaypointId() : BossSpawnPointId;
	if (Point && (HostShip.GetDeckWaypoint(OutPointId) != Point || !Point->CanUseInCombat()))
	{
		return false;
	}
	if (!Point)
	{
		Point = HostShip.GetDeckWaypoint(OutPointId);
	}
	if (!Point)
	{
		TArray<int32> PointIds;
		HostShip.GetDeckWaypointIds(PointIds, true);
		for (const int32 PointId : PointIds)
		{
			UDeckWaypointComponent* Candidate = HostShip.GetDeckWaypoint(PointId);
			if (Candidate && Candidate->CanSpawnEnemy())
			{
				OutPointId = PointId;
				Point = Candidate;
				break;
			}
		}
		if (!Point && !PointIds.IsEmpty())
		{
			OutPointId = PointIds[0];
			Point = HostShip.GetDeckWaypoint(OutPointId);
		}
	}

	if (!Point)
	{
		return false;
	}
	const AShipBossEnemy* BossCDO = BossClass ? BossClass->GetDefaultObject<AShipBossEnemy>() : nullptr;
	const UCapsuleComponent* Capsule = BossCDO ? BossCDO->GetCapsuleComponent() : nullptr;
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
	return HostShip.ResolveDeckCharacterTransform(OutPointId, HalfHeight, OutTransform)
		|| HostShip.ResolveFixedDeckAnchorTransform(OutPointId, HalfHeight, OutTransform);
}

void UBossEncounterComponent::BindItemBox()
{
	if (EnemyItemBox && EnemyItemBox->GetInteractableComponent())
	{
		EnemyItemBox->GetInteractableComponent()->OnInteracted.AddUniqueDynamic(
			this, &UBossEncounterComponent::HandleItemBoxInteracted);
	}
}

void UBossEncounterComponent::UnbindItemBox()
{
	if (EnemyItemBox && EnemyItemBox->GetInteractableComponent())
	{
		EnemyItemBox->GetInteractableComponent()->OnInteracted.RemoveDynamic(
			this, &UBossEncounterComponent::HandleItemBoxInteracted);
	}
}

void UBossEncounterComponent::SetEncounterState(EBossEncounterState NewState)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || EncounterState == NewState)
	{
		return;
	}
	const EBossEncounterState OldState = EncounterState;
	EncounterState = NewState;
	OnEncounterStateChanged.Broadcast(OldState, NewState);
	GetOwner()->ForceNetUpdate();
}
