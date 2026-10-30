#include "Components/ChestGroundGuardBindingComponent.h"

#include "BaseEnemy.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "Spawning/GroundEnemySpawner.h"

UChestGroundGuardBindingComponent::UChestGroundGuardBindingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
}

void UChestGroundGuardBindingComponent::InitializeComponent()
{
	Super::InitializeComponent();
	// Level components initialize before any actor BeginPlay, including auto-spawners.
	if (const AChestSpawnPoint* Point = Cast<AChestSpawnPoint>(GetOwner());
		IsValid(Point) && Point->HasAuthority() && IsValid(GuardSpawner))
	{
		GuardSpawner->OnEnemySpawned.AddUniqueDynamic(this,
			&UChestGroundGuardBindingComponent::HandleEnemySpawned);
	}
}

void UChestGroundGuardBindingComponent::UninitializeComponent()
{
	if (IsValid(GuardSpawner))
	{
		GuardSpawner->OnEnemySpawned.RemoveDynamic(this,
			&UChestGroundGuardBindingComponent::HandleEnemySpawned);
	}
	Super::UninitializeComponent();
}

void UChestGroundGuardBindingComponent::HandleEnemySpawned(ABaseEnemy* Enemy, FGameplayTag EnemyTypeTag)
{
	if (AChestSpawnPoint* Point = Cast<AChestSpawnPoint>(GetOwner()); IsValid(Point))
	{
		Point->RegisterGuardCharacter(Enemy);
	}
}
