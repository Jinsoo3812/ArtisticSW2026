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
#include "ItemSpawn/LootSpawnPoint.h"
#include "Engine/GameInstance.h"

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
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (UGameInstance* GameInstance = GetWorld()->GetGameInstance())
		{
			if (UStoryFacadeSubsystem* Story = GameInstance->GetSubsystem<UStoryFacadeSubsystem>())
			{
				Story->OnStoryChanged.AddUniqueDynamic(this, &UBossEncounterComponent::HandleStoryChanged);
			}
		}
		UpdateBossReservation();
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
		if (AChestSpawnPoint* Point = ResolveTriggerChestPoint())
		{
			Point->OnChestSpawned.AddUniqueDynamic(this, &UBossEncounterComponent::HandleChestSpawned);
			if (AStorageChest* Chest = Cast<AStorageChest>(Point->GetSpawnedActor())) HandleChestSpawned(Chest);
		}
		else if (GetOwner() && GetOwner()->HasAuthority())
		{
			UE_LOG(LogTemp, Error, TEXT("[BossEncounter] Missing trigger chest point on %s"), *GetNameSafe(GetOwner()));
		}
		BindItemBox();
	}
}

void UBossEncounterComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindItemBox();
	if (AChestSpawnPoint* Point = ResolveTriggerChestPoint())
	{
		Point->OnChestSpawned.RemoveDynamic(this, &UBossEncounterComponent::HandleChestSpawned);
	}
	if (UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (UStoryFacadeSubsystem* Story = GameInstance->GetSubsystem<UStoryFacadeSubsystem>())
		{
			Story->OnStoryChanged.RemoveDynamic(this, &UBossEncounterComponent::HandleStoryChanged);
		}
	}
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
	UpdateBossReservation();
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
		|| !IsCampaignGateOpen()
		|| (EncounterTrigger == EBossEncounterTrigger::ItemBoxInteraction && !ResolveTriggerChestPoint())
		|| !IsValid(TriggerActor))
	{
		return false;
	}
	if (!ResolveEncounterTarget(TriggerActor))
	{
		// Some game modes possess the Player Ship directly and never populate
		// RidingPlayer. Spawning the encounter must not depend on an initial
		// character target; the boss perception controller can acquire one later.
		UE_LOG(LogTemp, Log,
			TEXT("[BossEncounter] Starting without an initial combat target. Trigger=%s"),
			*GetNameSafe(TriggerActor));
	}

	// Change state before spawning so simultaneous interactions cannot create two bosses.
	SetEncounterState(EBossEncounterState::Spawning);
	if (!SpawnBossFor(TriggerActor))
	{
		SetEncounterState(EBossEncounterState::Failed);
		UpdateBossReservation();
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
	UpdateBossReservation();
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
	if (!HostShip || !World || !BossClass)
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

	if (!Boss->ConfigureSpawnBalance(BossStatsRow))
	{
		Boss->Destroy();
		return false;
	}
	Boss->FinishSpawning(SpawnTransform);
	if (!IsValid(Boss)) return false;
	if (!Boss->IsBalanceReady())
	{
		Boss->Destroy();
		return false;
	}
	if (!Boss->InitializeBoss(HostShip, SpawnPointId, CombatTarget))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BossEncounter] Boss initialization failed. Ship=%s PointId=%d Target=%s"),
			*GetNameSafe(HostShip), SpawnPointId, *GetNameSafe(CombatTarget));
		Boss->Destroy();
		return false;
	}

	SpawnedBoss = Boss;
	if (!HostShip->RegisterBossEnemy(Boss))
	{
		SpawnedBoss = nullptr;
		Boss->Destroy();
		return false;
	}
	TInlineComponentArray<UChildActorComponent*> ChestComponents(HostShip);
	for (UChildActorComponent* Component : ChestComponents)
	{
		if (AChestSpawnPoint* Point = Component ? Cast<AChestSpawnPoint>(Component->GetChildActor()) : nullptr)
		{
			if (Point->GetSpawnMode() == EChestSpawnMode::Guarded) Point->RegisterBossGuard(Boss);
		}
	}
	UE_LOG(LogTemp, Log,
		TEXT("[BossEncounter] Boss spawned. Ship=%s Boss=%s PointId=%d InitialTarget=%s"),
		*GetNameSafe(HostShip), *GetNameSafe(Boss), SpawnPointId, *GetNameSafe(CombatTarget));
	if (UBaseHealthComponent* Health = Boss->GetHealthComponent())
	{
		Health->OnDeathStarted.AddUniqueDynamic(this, &UBossEncounterComponent::HandleBossDeathStarted);
	}

	SetEncounterState(EBossEncounterState::Active);
	UpdateBossReservation();
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
	OutPointId = BossSpawnPointId;
	UDeckWaypointComponent* Point = HostShip.GetDeckWaypoint(OutPointId);
	if (!Point || !Point->CanUseInCombat())
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

AChestSpawnPoint* UBossEncounterComponent::ResolveTriggerChestPoint() const
{
	UChildActorComponent* Component = Cast<UChildActorComponent>(
		TriggerChestSpawnPointComponent.GetComponent(GetOwner()));
	return Component ? Cast<AChestSpawnPoint>(Component->GetChildActor()) : nullptr;
}

bool UBossEncounterComponent::IsCampaignGateOpen() const
{
	if (!bEncounterEnabled || !GetOwner() || !GetOwner()->HasAuthority()) return false;
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UStoryFacadeSubsystem* Story = GameInstance
		? GameInstance->GetSubsystem<UStoryFacadeSubsystem>() : nullptr;
	if (!Story)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossEncounter] Story subsystem missing on %s"), *GetNameSafe(GetOwner()));
		return false;
	}
	return Story->IsStoryNodeReached(RequiredStoryNode)
		&& !Story->IsStoryNodeReached(StopAfterStoryNode);
}

void UBossEncounterComponent::UpdateBossReservation()
{
	AEnemyShip* HostShip = Cast<AEnemyShip>(GetOwner());
	if (!HostShip || !HostShip->HasAuthority()) return;
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UStoryFacadeSubsystem* Story = GameInstance
		? GameInstance->GetSubsystem<UStoryFacadeSubsystem>() : nullptr;
	const bool bStoppedWithoutBoss = Story && Story->IsStoryNodeReached(StopAfterStoryNode)
		&& !IsValid(SpawnedBoss);
	const bool bReserved = bEncounterEnabled && !bStoppedWithoutBoss
		&& (EncounterState == EBossEncounterState::Waiting
			|| EncounterState == EBossEncounterState::Spawning);
	TInlineComponentArray<UChildActorComponent*> Components(HostShip);
	for (UChildActorComponent* Component : Components)
	{
		if (AChestSpawnPoint* Point = Component ? Cast<AChestSpawnPoint>(Component->GetChildActor()) : nullptr)
		{
			if (Point->GetSpawnMode() == EChestSpawnMode::Guarded) Point->SetBossEncounterReserved(bReserved);
		}
	}
}

void UBossEncounterComponent::HandleStoryChanged()
{
	UpdateBossReservation();
}

void UBossEncounterComponent::HandleChestSpawned(AStorageChest* Chest)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsValid(Chest)) return;
	UnbindItemBox();
	EnemyItemBox = Chest;
	BindItemBox();
}
