#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseEnemy.h"
#include "BaseGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Ship.h"
#include "ShipAttributeSet.h"
#include "ShipAI/EnemyShip.h"
#include "UObject/UObjectHash.h"
#include "WaterBombCannonball.h"

namespace WaterBombIntegrationTests
{
	UBaseAttributeSet* FindBaseAttributeSet(AActor* Owner)
	{
		if (!Owner)
		{
			return nullptr;
		}

		TArray<UObject*> Subobjects;
		GetObjectsWithOuter(Owner, Subobjects, false);
		for (UObject* Subobject : Subobjects)
		{
			if (UBaseAttributeSet* AttributeSet = Cast<UBaseAttributeSet>(Subobject))
			{
				return AttributeSet;
			}
		}

		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWaterBombProjectileIntegrationTest,
	"ArtisticSW.WaterBomb.ProjectileHitIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWaterBombProjectileIntegrationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("WaterBombProjectileIntegrationWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	auto CleanupWorld = [World]()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	AShip* SourceShip = World->SpawnActor<AShip>();
	AEnemyShip* TargetShip = World->SpawnActor<AEnemyShip>();
	ABaseEnemy* OnboardEnemy = World->SpawnActor<ABaseEnemy>();
	AWaterBombCannonball* Projectile = World->SpawnActor<AWaterBombCannonball>();
	if (!TestNotNull(TEXT("Player source ship is spawned"), SourceShip)
		|| !TestNotNull(TEXT("Enemy target ship is spawned"), TargetShip)
		|| !TestNotNull(TEXT("Onboard enemy is spawned"), OnboardEnemy)
		|| !TestNotNull(TEXT("Water-bomb projectile is spawned"), Projectile))
	{
		CleanupWorld();
		return false;
	}

	UAbilitySystemComponent* SourceShipASC = SourceShip->GetAbilitySystemComponent();
	UAbilitySystemComponent* TargetShipASC = TargetShip->GetAbilitySystemComponent();
	UAbilitySystemComponent* EnemyASC = OnboardEnemy->GetAbilitySystemComponent();
	UBaseAttributeSet* EnemyAttributes = WaterBombIntegrationTests::FindBaseAttributeSet(OnboardEnemy);
	if (!TestNotNull(TEXT("Source ship ASC exists"), SourceShipASC)
		|| !TestNotNull(TEXT("Target ship ASC exists"), TargetShipASC)
		|| !TestNotNull(TEXT("Enemy ASC exists"), EnemyASC)
		|| !TestNotNull(TEXT("Enemy AttributeSet exists"), EnemyAttributes))
	{
		CleanupWorld();
		return false;
	}

	SourceShipASC->AddSpawnedAttribute(SourceShip->GetShipAttributeSet());
	SourceShipASC->InitAbilityActorInfo(SourceShip, SourceShip);
	TargetShipASC->AddSpawnedAttribute(TargetShip->GetShipAttributeSet());
	TargetShipASC->InitAbilityActorInfo(TargetShip, TargetShip);
	EnemyASC->AddSpawnedAttribute(EnemyAttributes);
	EnemyASC->InitAbilityActorInfo(OnboardEnemy, OnboardEnemy);

	OnboardEnemy->AttachToActor(TargetShip, FAttachmentTransformRules::KeepWorldTransform);
	TestTrue(TEXT("Test enemy is recognized as based on/attached to target ship"),
		OnboardEnemy->IsBasedOnActor(TargetShip) || OnboardEnemy->GetAttachParentActor() == TargetShip);

	Projectile->InitializeProjectile(SourceShip, 0.0f, 1000.0f);
	Projectile->HandleShipHit(TargetShip);

	TestTrue(TEXT("Enemy ship receives cannon-disable tag"),
		TargetShipASC->HasMatchingGameplayTag(State_Ship_CannonDisabled));
	TestTrue(TEXT("Onboard enemy receives water-bomb slow tag"),
		EnemyASC->HasMatchingGameplayTag(State_Debuff_WaterBomb));
	TestEqual(TEXT("Onboard enemy attack speed is halved"),
		EnemyASC->GetNumericAttribute(UBaseAttributeSet::GetAttackSpeedMultiplierAttribute()),
		0.5f);
	TestTrue(TEXT("Projectile is destroyed after valid enemy-ship hit"), Projectile->IsActorBeingDestroyed());

	FGameplayTagContainer SlowTagContainer;
	SlowTagContainer.AddTag(State_Debuff_WaterBomb);
	EnemyASC->RemoveActiveEffectsWithGrantedTags(SlowTagContainer);
	TestEqual(TEXT("Removing slow GE restores attack speed"),
		EnemyASC->GetNumericAttribute(UBaseAttributeSet::GetAttackSpeedMultiplierAttribute()),
		1.0f);

	CleanupWorld();
	return true;
}

#endif
