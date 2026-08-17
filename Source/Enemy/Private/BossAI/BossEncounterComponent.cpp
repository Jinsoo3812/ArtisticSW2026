#include "BossAI/BossEncounterComponent.h"

#include "BossAI/ShipBossEnemy.h"
#include "Components/BaseHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "Engine/World.h"
#include "Interactable/InteractableComponent.h"
#include "Net/UnrealNetwork.h"
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

	BindItemBox();
	if (GetOwner()->HasAuthority() && EnemyItemBox)
	{
		EnemyItemBox->SetPhysicsAndBuoyancyEnabled(false);
		EnemyItemBox->SetLocked(true);
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
	BindItemBox();
	if (AEnemyShip* HostShip = Cast<AEnemyShip>(GetOwner()))
	{
		HostShip->OnDestroyed.AddUniqueDynamic(this, &UBossEncounterComponent::HandleHostShipDestroyed);
	}
	if (EnemyItemBox)
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
	if (!GetOwner() || !GetOwner()->HasAuthority()
		|| EncounterState != EBossEncounterState::Waiting
		|| !IsValid(Interactor))
	{
		return;
	}

	// Change state before spawning so simultaneous interactions cannot create two bosses.
	SetEncounterState(EBossEncounterState::Spawning);
	if (!SpawnBossFor(Interactor))
	{
		SetEncounterState(EBossEncounterState::Failed);
		if (EnemyItemBox)
		{
			EnemyItemBox->SetLocked(true);
		}
	}
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
	if (!HostShip || !World || !BossClass || !EnemyItemBox)
	{
		return false;
	}

	int32 SpawnPointId = INDEX_NONE;
	FTransform SpawnTransform;
	if (!ResolveSpawnPoint(*HostShip, SpawnPointId, SpawnTransform))
	{
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
		return false;
	}

	Boss->FinishSpawning(SpawnTransform);
	if (!Boss->InitializeBoss(HostShip, SpawnPointId, Interactor))
	{
		Boss->Destroy();
		return false;
	}

	SpawnedBoss = Boss;
	if (UBaseHealthComponent* Health = Boss->GetHealthComponent())
	{
		Health->OnDeathStarted.AddUniqueDynamic(this, &UBossEncounterComponent::HandleBossDeathStarted);
	}

	TArray<ABaseCharacter*> Guards;
	Guards.Add(Boss);
	EnemyItemBox->ConfigureGuarding(true, Guards, HostShip);
	SetEncounterState(EBossEncounterState::Active);
	return true;
}

bool UBossEncounterComponent::ResolveSpawnPoint(
	AEnemyShip& HostShip,
	int32& OutPointId,
	FTransform& OutTransform) const
{
	OutPointId = BossSpawnPointId;
	UDeckWaypointComponent* Point = HostShip.GetDeckWaypoint(OutPointId);
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
	return HostShip.ResolveDeckCharacterTransform(OutPointId, HalfHeight, OutTransform);
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
