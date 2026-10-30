#include "Spawning/CampaignBossSpawner.h"

#include "BaseEnemy.h"
#include "AI/EnemyTerritoryComponent.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "Engine/GameInstance.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"
#include "Room/SWRoomSnapshotSubsystem.h"

void ACampaignBossSpawner::BeginPlay()
{
	OnActorSpawned.AddUniqueDynamic(this, &ACampaignBossSpawner::ConfigureSpawnedBoss);
	if (UGameInstance* GameInstance = GetGameInstance())
		if (UStoryFacadeSubsystem* Story = GameInstance->GetSubsystem<UStoryFacadeSubsystem>())
			Story->OnStoryChanged.AddUniqueDynamic(this, &ACampaignBossSpawner::RefreshRewardReservation);
	RefreshRewardReservation();
	Super::BeginPlay();
}

void ACampaignBossSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameInstance* GameInstance = GetGameInstance())
		if (UStoryFacadeSubsystem* Story = GameInstance->GetSubsystem<UStoryFacadeSubsystem>())
			Story->OnStoryChanged.RemoveDynamic(this, &ACampaignBossSpawner::RefreshRewardReservation);
	Super::EndPlay(EndPlayReason);
}

void ACampaignBossSpawner::RefreshRewardReservation()
{
	if (!HasAuthority() || !RewardChest) return;
	const UGameInstance* GameInstance = GetGameInstance();
	const UStoryFacadeSubsystem* Story = GameInstance ? GameInstance->GetSubsystem<UStoryFacadeSubsystem>() : nullptr;
	RewardChest->SetBossEncounterReserved(!GetSpawnedActor() && (!Story || !Story->IsStoryNodeReached(StopAfterStoryNode)));
}

void ACampaignBossSpawner::ConfigureSpawnedBoss(AActor* Actor)
{
	ABaseEnemy* Boss = Cast<ABaseEnemy>(Actor);
	if (!HasAuthority() || !Boss) return;
	Boss->bCompleteStoryNodeOnDeath = true;
	Boss->CompletedStoryNodeOnDeath = StopAfterStoryNode;
	if (!GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()->IsRestoringSnapshot())
		if (UNavigationSystemV1* Navigation = UNavigationSystemV1::GetCurrent(GetWorld()))
		{
			FNavLocation Floor;
			if (Navigation->ProjectPointToNavigation(GetActorLocation(), Floor, FVector(1000.f)))
				Boss->TeleportTo(Floor.Location + FVector(0, 0, Boss->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), GetActorRotation(), false, true);
		}
	if (UEnemyTerritoryComponent* Territory = Boss->FindComponentByClass<UEnemyTerritoryComponent>())
		Territory->InitializeTerritory(GetActorLocation(), 1200.f, 3000.f);
	if (RewardChest)
	{
		RewardChest->RegisterBossGuard(Boss);
		RewardChest->SetBossEncounterReserved(false);
	}
}

bool ACampaignBossSpawner::FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError)
{
	if (!Super::FinalizeRoomRestore(RegisteredActors, OutError)) return false;
	if (AActor* Boss = GetSpawnedActor()) ConfigureSpawnedBoss(Boss);
	return true;
}
