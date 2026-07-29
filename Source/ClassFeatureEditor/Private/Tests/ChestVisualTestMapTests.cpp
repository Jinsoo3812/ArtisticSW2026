#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BaseCharacter.h"
#include "HAL/FileManager.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "ItemSpawn/ChestSpawnData.h"
#include "ItemSpawn/GlobalLootSpawnManager.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "Misc/PackageName.h"
#include "Ship.h"
#include "Storage/StorageChest.h"

namespace ChestVisualTestMap
{
	const FString SourceMap = TEXT("/Game/New/Level/Test_Level");
	const FString TargetMap = TEXT("/Game/Tests/ChestSystem/ChestSystem_Test_Level");

	const TCHAR* LowDefinitionPath = TEXT("/Game/Tests/ChestSystem/DA_ChestTest_Low.DA_ChestTest_Low");
	const TCHAR* HighDefinitionPath = TEXT("/Game/Tests/ChestSystem/DA_ChestTest_High.DA_ChestTest_High");
	const TCHAR* LowGroupPath = TEXT("/Game/Tests/ChestSystem/DA_ChestTestGroup_Low.DA_ChestTestGroup_Low");
	const TCHAR* HighGroupPath = TEXT("/Game/Tests/ChestSystem/DA_ChestTestGroup_High.DA_ChestTestGroup_High");
	const TCHAR* GuardClassPath = TEXT("/Game/GameplayAbilitySystem/Enemy/BP_MyEnemy.BP_MyEnemy_C");
	const TCHAR* ShipClassPath = TEXT("/Game/New/Enemy_Ship/BP_EnemyShip.BP_EnemyShip_C");

	UWorld* LoadMap(const FString& LongPackageName)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(
			LongPackageName,
			FPackageName::GetMapPackageExtension());
		return UEditorLoadingAndSavingUtils::LoadMap(Filename);
	}

	void LabelActor(AActor* Actor, const TCHAR* Label)
	{
		if (Actor)
		{
			Actor->SetActorLabel(Label);
			Actor->SetFolderPath(TEXT("ChestSystem_Automation"));
		}
	}

	AActor* FindActorByLabel(UWorld* World, const FString& Label)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Label)
			{
				return *It;
			}
		}
		return nullptr;
	}

	int32 CountActivatedPointsWithPrefix(UWorld* World, const FString& Prefix)
	{
		int32 Count = 0;
		for (TActorIterator<AChestSpawnPoint> It(World); It; ++It)
		{
			if (It->GetActorLabel().StartsWith(Prefix) && It->IsActivated())
			{
				++Count;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBuildChestVisualTestMap,
	"ArtisticSW.Tools.BuildChestVisualTestMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBuildChestVisualTestMap::RunTest(const FString& Parameters)
{
	using namespace ChestVisualTestMap;

	const FString TargetFilename = FPackageName::LongPackageNameToFilename(
		TargetMap,
		FPackageName::GetMapPackageExtension());
	if (IFileManager::Get().FileExists(*TargetFilename))
	{
		TestTrue(TEXT("Previous generated test map is removed"),
			IFileManager::Get().Delete(*TargetFilename, false, true, true));
	}

	UWorld* World = LoadMap(SourceMap);
	if (!TestNotNull(TEXT("Source Test_Level loads"), World))
	{
		return false;
	}

	UChestDefinition* LowDefinition = LoadObject<UChestDefinition>(nullptr, LowDefinitionPath);
	UChestDefinition* HighDefinition = LoadObject<UChestDefinition>(nullptr, HighDefinitionPath);
	URandomChestGroup* LowGroup = LoadObject<URandomChestGroup>(nullptr, LowGroupPath);
	URandomChestGroup* HighGroup = LoadObject<URandomChestGroup>(nullptr, HighGroupPath);
	UClass* GuardClass = LoadObject<UClass>(nullptr, GuardClassPath);
	UClass* ShipClass = LoadObject<UClass>(nullptr, ShipClassPath);
	if (!TestNotNull(TEXT("Low chest definition loads"), LowDefinition)
		|| !TestNotNull(TEXT("High chest definition loads"), HighDefinition)
		|| !TestNotNull(TEXT("Low random group loads"), LowGroup)
		|| !TestNotNull(TEXT("High random group loads"), HighGroup)
		|| !TestNotNull(TEXT("Visual guard Blueprint class loads"), GuardClass)
		|| !TestNotNull(TEXT("Visual enemy ship Blueprint class loads"), ShipClass))
	{
		return false;
	}

	FVector Anchor(1500.0, 2500.0, 100.0);
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Anchor += It->GetActorLocation();
		break;
	}

	FVector ShipAnchor = Anchor + FVector(0.0, 3000.0, 0.0);
	for (TActorIterator<AShip> It(World); It; ++It)
	{
		if (It->IsA(ShipClass))
		{
			ShipAnchor = It->GetActorLocation() + FVector(1500.0, 0.0, 0.0);
			break;
		}
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AGlobalLootSpawnManager* Manager = World->SpawnActor<AGlobalLootSpawnManager>(
		AGlobalLootSpawnManager::StaticClass(),
		FTransform(Anchor + FVector(-500.0, -500.0, 0.0)),
		SpawnParameters);
	LabelActor(Manager, TEXT("ChestTest_GlobalLootSpawnManager"));

	for (int32 Index = 0; Index < 3; ++Index)
	{
		AChestSpawnPoint* Point = World->SpawnActor<AChestSpawnPoint>(
			AChestSpawnPoint::StaticClass(),
			FTransform(Anchor + FVector(Index * 350.0, 0.0, 0.0)),
			SpawnParameters);
		LabelActor(Point, *FString::Printf(TEXT("ChestTest_RandomLow_%d"), Index + 1));
		Point->ConfigureRandomSpawn(LowGroup);
	}

	for (int32 Index = 0; Index < 2; ++Index)
	{
		AChestSpawnPoint* Point = World->SpawnActor<AChestSpawnPoint>(
			AChestSpawnPoint::StaticClass(),
			FTransform(Anchor + FVector(Index * 350.0, 600.0, 0.0)),
			SpawnParameters);
		LabelActor(Point, *FString::Printf(TEXT("ChestTest_RandomHigh_%d"), Index + 1));
		Point->ConfigureRandomSpawn(HighGroup);
	}

	ABaseCharacter* IslandGuardA = Cast<ABaseCharacter>(World->SpawnActor<AActor>(
		GuardClass,
		FTransform(Anchor + FVector(-350.0, 1400.0, 0.0)),
		SpawnParameters));
	ABaseCharacter* IslandGuardB = Cast<ABaseCharacter>(World->SpawnActor<AActor>(
		GuardClass,
		FTransform(Anchor + FVector(350.0, 1400.0, 0.0)),
		SpawnParameters));
	LabelActor(IslandGuardA, TEXT("ChestTest_IslandGuard_A"));
	LabelActor(IslandGuardB, TEXT("ChestTest_IslandGuard_B"));

	AChestSpawnPoint* IslandPoint = World->SpawnActor<AChestSpawnPoint>(
		AChestSpawnPoint::StaticClass(),
		FTransform(Anchor + FVector(0.0, 1400.0, 0.0)),
		SpawnParameters);
	LabelActor(IslandPoint, TEXT("ChestTest_IslandGuarded"));
	IslandPoint->ConfigureGuardedSpawn(LowDefinition, {IslandGuardA, IslandGuardB}, nullptr);

	AShip* TestShip = Cast<AShip>(World->SpawnActor<AActor>(
		ShipClass,
		FTransform(ShipAnchor),
		SpawnParameters));
	ABaseCharacter* ShipGuard = Cast<ABaseCharacter>(World->SpawnActor<AActor>(
		GuardClass,
		FTransform(ShipAnchor + FVector(250.0, 0.0, 250.0)),
		SpawnParameters));
	LabelActor(TestShip, TEXT("ChestTest_EnemyShip"));
	LabelActor(ShipGuard, TEXT("ChestTest_ShipGuard"));

	AChestSpawnPoint* ShipPoint = World->SpawnActor<AChestSpawnPoint>(
		AChestSpawnPoint::StaticClass(),
		FTransform(ShipAnchor + FVector(0.0, 0.0, 250.0)),
		SpawnParameters);
	LabelActor(ShipPoint, TEXT("ChestTest_ShipGuarded"));
	ShipPoint->ConfigureGuardedSpawn(HighDefinition, {ShipGuard}, TestShip);

	if (!TestNotNull(TEXT("Global manager is placed"), Manager)
		|| !TestNotNull(TEXT("Island guard A is placed"), IslandGuardA)
		|| !TestNotNull(TEXT("Island guard B is placed"), IslandGuardB)
		|| !TestNotNull(TEXT("Island guarded point is placed"), IslandPoint)
		|| !TestNotNull(TEXT("Enemy ship is placed"), TestShip)
		|| !TestNotNull(TEXT("Ship guard is placed"), ShipGuard)
		|| !TestNotNull(TEXT("Ship guarded point is placed"), ShipPoint))
	{
		return false;
	}

	TestTrue(TEXT("Generated visual test map saves"),
		UEditorLoadingAndSavingUtils::SaveMap(World, TargetMap));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChestPlacedMapLogicTest,
	"ArtisticSW.Chest.MapPlacedLogic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChestPlacedMapLogicTest::RunTest(const FString& Parameters)
{
	using namespace ChestVisualTestMap;

	UWorld* World = LoadMap(TargetMap);
	if (!TestNotNull(TEXT("Generated chest test map loads"), World))
	{
		return false;
	}

	AGlobalLootSpawnManager* Manager = Cast<AGlobalLootSpawnManager>(
		FindActorByLabel(World, TEXT("ChestTest_GlobalLootSpawnManager")));
	if (!TestNotNull(TEXT("Placed global manager exists"), Manager))
	{
		return false;
	}

	TestEqual(TEXT("Placed map spawns two low, one high, and two guarded chests"),
		Manager->InitializeDataDrivenChests(), 5);
	TestEqual(TEXT("Low random group activates two of three points"),
		CountActivatedPointsWithPrefix(World, TEXT("ChestTest_RandomLow_")), 2);
	TestEqual(TEXT("High random group activates one of two points"),
		CountActivatedPointsWithPrefix(World, TEXT("ChestTest_RandomHigh_")), 1);

	AChestSpawnPoint* IslandPoint = Cast<AChestSpawnPoint>(
		FindActorByLabel(World, TEXT("ChestTest_IslandGuarded")));
	AStorageChest* IslandChest = IslandPoint
		? Cast<AStorageChest>(IslandPoint->GetSpawnedActor())
		: nullptr;

	if (!TestNotNull(TEXT("Placed island chest spawns"), IslandChest))
	{
		return false;
	}

	TestTrue(TEXT("Placed island chest starts locked"), IslandChest->IsLocked());
	TestTrue(TEXT("Placed island chest requires guard clear"), IslandChest->RequiresGuardClear());
	TestEqual(TEXT("Placed island chest tracks both assigned guards"),
		IslandChest->GetAliveGuardCount(), 2);

	AChestSpawnPoint* ShipPoint = Cast<AChestSpawnPoint>(
		FindActorByLabel(World, TEXT("ChestTest_ShipGuarded")));
	AStorageChest* ShipChest = ShipPoint
		? Cast<AStorageChest>(ShipPoint->GetSpawnedActor())
		: nullptr;

	if (!TestNotNull(TEXT("Placed ship chest spawns"), ShipChest))
	{
		return false;
	}

	TestTrue(TEXT("Placed ship chest starts locked"), ShipChest->IsLocked());
	TestTrue(TEXT("Placed ship chest requires guard clear"), ShipChest->RequiresGuardClear());
	TestEqual(TEXT("Placed ship chest tracks its assigned deck guard"),
		ShipChest->GetAliveGuardCount(), 1);
	TestFalse(TEXT("Placed ship chest disables independent physics"),
		ShipChest->IsPhysicsAndBuoyancyEnabled());
	return true;
}

#endif
