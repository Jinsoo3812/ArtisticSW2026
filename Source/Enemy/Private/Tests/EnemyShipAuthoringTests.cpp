#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "Cannon.h"
#include "Components/ChildActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "Misc/DataValidation.h"
#include "ShipAI/Abilities/GA_EnemyShipCannonVolley.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipArchetypeData.h"
#include "ShipAI/EnemyShipSkillModuleData.h"
#include "Ship.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipChestSpawnPointAuthoringTest,
	"ArtisticSW.Enemy.Ship.Authoring.ChestSpawnPointSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipChestSpawnPointAuthoringTest::RunTest(const FString& Parameters)
{
	const FStructProperty* ChestProperty = FindFProperty<FStructProperty>(
		AEnemyShip::StaticClass(),
		GET_MEMBER_NAME_CHECKED(AEnemyShip, ChestSpawnPointChestSettings));
	const FStructProperty* LootProperty = FindFProperty<FStructProperty>(
		AEnemyShip::StaticClass(),
		GET_MEMBER_NAME_CHECKED(AEnemyShip, ChestSpawnPointLootSettings));

	if (TestNotNull(TEXT("Enemy Ship exposes Chest settings"), ChestProperty))
	{
		TestTrue(TEXT("Chest settings are instance editable"), ChestProperty->HasAnyPropertyFlags(CPF_Edit));
		TestEqual(TEXT("Chest settings use the Chest category"), ChestProperty->GetMetaData(TEXT("Category")), FString(TEXT("Chest")));
		TestTrue(TEXT("Chest settings are flattened into the category"), ChestProperty->HasMetaData(TEXT("ShowOnlyInnerProperties")));
	}
	if (TestNotNull(TEXT("Enemy Ship exposes Loot settings"), LootProperty))
	{
		TestTrue(TEXT("Loot settings are instance editable"), LootProperty->HasAnyPropertyFlags(CPF_Edit));
		TestEqual(TEXT("Loot settings use the Loot category"), LootProperty->GetMetaData(TEXT("Category")), FString(TEXT("Loot")));
		TestTrue(TEXT("Loot settings are flattened into the category"), LootProperty->HasMetaData(TEXT("ShowOnlyInnerProperties")));
	}

	const FProperty* BossField = FindFProperty<FProperty>(
		FChestSpawnPointChestSettings::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FChestSpawnPointChestSettings, bIsBossChest));
	const FProperty* LootSpawnField = FindFProperty<FProperty>(
		FChestSpawnPointLootSettings::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FChestSpawnPointLootSettings, bEnabled));
	if (TestNotNull(TEXT("Chest struct exposes its Boss section"), BossField))
	{
		// Boss encounters register their guards at runtime; keep legacy serialization only.
		TestTrue(TEXT("Legacy boss checkbox is deprecated"), BossField->HasMetaData(TEXT("DeprecatedProperty")));
		TestFalse(TEXT("Legacy boss checkbox cannot author runtime boss state"), BossField->HasAnyPropertyFlags(CPF_Edit));
	}
	if (TestNotNull(TEXT("Loot struct exposes its Spawn section"), LootSpawnField))
	{
		TestEqual(TEXT("Spawn field stays nested below Loot"), LootSpawnField->GetMetaData(TEXT("Category")), FString(TEXT("Loot|Spawn")));
	}

	AChestSpawnPoint* SpawnPoint = NewObject<AChestSpawnPoint>();
	FChestSpawnPointChestSettings ChestSettings;
	ChestSettings.SpawnMode = EChestSpawnMode::Random;
	ChestSettings.Environment = EChestEnvironment::Water;
	FChestSpawnPointLootSettings LootSettings;
	LootSettings.PointWeight = 2.5f;
	SpawnPoint->ApplyAuthoringSettings(ChestSettings, LootSettings);

	TestEqual(TEXT("Chest spawn mode is forwarded"), SpawnPoint->GetSpawnMode(), EChestSpawnMode::Random);
	TestEqual(TEXT("Chest environment is forwarded"), SpawnPoint->GetEnvironment(), EChestEnvironment::Water);
	TestEqual(TEXT("Loot point weight is forwarded"), SpawnPoint->GetPointWeight(), 2.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipThreeStageAuthoringTest,
	"ArtisticSW.Enemy.Ship.Authoring.ThreeStageAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipThreeStageAuthoringTest::RunTest(const FString& Parameters)
{
	UClass* ShipClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip.BP_EnemyShip_C"));
	AEnemyShip* ShipCDO = ShipClass ? Cast<AEnemyShip>(ShipClass->GetDefaultObject()) : nullptr;
	if (!TestNotNull(TEXT("BP_EnemyShip loads"), ShipCDO))
	{
		return false;
	}

	UEnemyShipArchetypeData* Archetype = LoadObject<UEnemyShipArchetypeData>(
		nullptr,
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/Normal/DA_ES_Normal_1.DA_ES_Normal_1"));
	UEnemyShipSkillModuleData* CannonModule = LoadObject<UEnemyShipSkillModuleData>(
		nullptr,
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/SkillModule/DA_ES_SkillModule_Cannon.DA_ES_SkillModule_Cannon"));
	if (!TestNotNull(TEXT("Cannon Archetype loads"), Archetype)
		|| !TestNotNull(TEXT("Cannon Skill Module loads"), CannonModule))
	{
		return false;
	}

	TestNotNull(TEXT("Archetype has a DT row"), Archetype->SpecRow.DataTable.Get());
	TestEqual(TEXT("Archetype contains one Cannon skill"), Archetype->SkillModules.Num(), 1);
	TestTrue(
		TEXT("Archetype directly references Cannon skill"),
		Archetype->SkillModules.Num() == 1 && Archetype->SkillModules[0] == CannonModule);
	TestTrue(
		TEXT("Cannon module directly references CannonVolley GA"),
		CannonModule->AbilityClass
			&& CannonModule->AbilityClass->IsChildOf(UGA_EnemyShipCannonVolley::StaticClass()));
	TestTrue(TEXT("Cannon skill derives its ability tag"), CannonModule->GetAbilityTag() == GameplayAbility_EnemyShip_CannonVolley);
	TestTrue(TEXT("Cannon skill allows Orbit"), CannonModule->AllowedNavigationStates.Contains(ENavalCombatState::Orbit));

	FDataValidationContext ValidationContext;
	TestFalse(TEXT("Cannon module validates"), CannonModule->IsDataValid(ValidationContext) == EDataValidationResult::Invalid);
	TestFalse(TEXT("Cannon Archetype validates"), Archetype->IsDataValid(ValidationContext) == EDataValidationResult::Invalid);

	const TCHAR* ModulePaths[] = {
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/SkillModule/DA_ES_SkillModule_Cannon.DA_ES_SkillModule_Cannon"),
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/SkillModule/DA_ES_SkillModule_Charge.DA_ES_SkillModule_Charge"),
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/SkillModule/DA_ES_SkillModule_Obstacle.DA_ES_SkillModule_Obstacle"),
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/SkillModule/DA_ES_SkillModule_TimeStop.DA_ES_SkillModule_TimeStop"),
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/SkillModule/DA_ES_SkillModule_Torpedo.DA_ES_SkillModule_Torpedo"),
	};
	for (const TCHAR* ModulePath : ModulePaths)
	{
		UEnemyShipSkillModuleData* Module = LoadObject<UEnemyShipSkillModuleData>(nullptr, ModulePath);
		TestNotNull(*FString::Printf(TEXT("Skill module loads: %s"), ModulePath), Module);
		if (Module)
		{
			FDataValidationContext ModuleContext;
			TestFalse(
				*FString::Printf(TEXT("Skill module validates: %s"), ModulePath),
				Module->IsDataValid(ModuleContext) == EDataValidationResult::Invalid);
		}
	}

	const TCHAR* ArchetypePaths[] = {
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/Normal/DA_ES_Normal_1.DA_ES_Normal_1"),
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/Normal/DA_ES_Normal_2.DA_ES_Normal_2"),
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/Normal/DA_ES_Normal_3.DA_ES_Normal_3"),
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/Normal/DA_ES_Normal_4.DA_ES_Normal_4"),
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/Elite/DA_ES_Charge.DA_ES_Charge"),
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/Elite/DA_ES_Obstacle.DA_ES_Obstacle"),
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/Elite/DA_ES_TimeStop.DA_ES_TimeStop"),
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/Elite/DA_ES_Torpedo.DA_ES_Torpedo"),
	};
	for (const TCHAR* ArchetypePath : ArchetypePaths)
	{
		UEnemyShipArchetypeData* LoadedArchetype = LoadObject<UEnemyShipArchetypeData>(nullptr, ArchetypePath);
		TestNotNull(*FString::Printf(TEXT("Archetype loads: %s"), ArchetypePath), LoadedArchetype);
		if (LoadedArchetype)
		{
			FDataValidationContext ArchetypeContext;
			TestFalse(
				*FString::Printf(TEXT("Archetype validates: %s"), ArchetypePath),
				LoadedArchetype->IsDataValid(ArchetypeContext) == EDataValidationResult::Invalid);
			TestEqual(
				*FString::Printf(TEXT("Zero-health cooldown is preserved: %s"), ArchetypePath),
				LoadedArchetype->ZeroHealthCannonCooldownMultiplier,
				3.0f);
		}
	}

	UBlueprint* ShipBlueprint = LoadObject<UBlueprint>(
		nullptr,
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip.BP_EnemyShip"));
	int32 CannonCount = 0;
	int32 ChestSpawnPointCount = 0;
	const TArray<USCS_Node*> Nodes = ShipBlueprint && ShipBlueprint->SimpleConstructionScript
		? ShipBlueprint->SimpleConstructionScript->GetAllNodes()
		: TArray<USCS_Node*>();
	for (const USCS_Node* Node : Nodes)
	{
		const UChildActorComponent* ChildActor = Node ? Cast<UChildActorComponent>(Node->ComponentTemplate) : nullptr;
		if (ChildActor && ChildActor->GetChildActorClass()
			&& ChildActor->GetChildActorClass()->IsChildOf(ACannon::StaticClass()))
		{
			++CannonCount;
		}
		if (ChildActor && ChildActor->GetChildActorClass()
			&& ChildActor->GetChildActorClass()->IsChildOf(AChestSpawnPoint::StaticClass()))
		{
			++ChestSpawnPointCount;
		}
	}
	TestEqual(TEXT("Enemy ship Blueprint authors exactly two cannons"), CannonCount, 2);
	TestEqual(TEXT("Enemy ship Blueprint authors exactly one chest spawn point"), ChestSpawnPointCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipFleetAuthoringTest,
	"ArtisticSW.Enemy.Ship.Authoring.EnemyFleetAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipFleetAuthoringTest::RunTest(const FString& Parameters)
{
	UDataTable* StatTable = LoadObject<UDataTable>(
		nullptr,
		TEXT("/Game/Blueprints/Item/Data/ShipUpgrade/DT_ShipStat.DT_ShipStat"));
	if (!TestNotNull(TEXT("DT_ShipStat loads"), StatTable))
	{
		return false;
	}

	UEnemyShipSkillModuleData* CannonModule = LoadObject<UEnemyShipSkillModuleData>(
		nullptr,
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/SkillModule/DA_ES_SkillModule_Cannon.DA_ES_SkillModule_Cannon"));

	for (int32 Index = 0; Index < 4; ++Index)
	{
		const int32 Tier = Index + 1;
		const FName RowName(*FString::Printf(TEXT("EnemyShip_Normal_%d"), Tier));
		const FShipStatRow* Row = StatTable->FindRow<FShipStatRow>(RowName, TEXT("Enemy Fleet Authoring Test"));
		if (!TestNotNull(*FString::Printf(TEXT("Normal %d DT row exists"), Tier), Row))
		{
			continue;
		}
		// Balance values are designer-owned. Test valid tier wiring and progression,
		// rather than freezing the earlier exponential balance table.
		TestTrue(TEXT("Fleet health and cannon damage are positive"), Row->MaxHealth > 0.0f && Row->CannonDamage > 0.0f);
		TestTrue(TEXT("Fleet propulsion and turn are positive"), Row->ForwardPropulsionMultiplier > 0.0f && Row->TurnTorqueMultiplier > 0.0f);
		TestTrue(TEXT("Fleet cannon cooldown is finite and positive"), FMath::IsFinite(Row->CannonFireCooldown) && Row->CannonFireCooldown > 0.0f);
		if (Index > 0)
		{
			const FName PreviousName(*FString::Printf(TEXT("EnemyShip_Normal_%d"), Tier - 1));
			if (const FShipStatRow* Previous = StatTable->FindRow<FShipStatRow>(PreviousName, TEXT("Fleet tier progression")))
			{
				TestTrue(TEXT("Later normal tiers retain health and damage progression"),
					Row->MaxHealth >= Previous->MaxHealth && Row->CannonDamage >= Previous->CannonDamage);
			}
		}

		const FString AssetName = FString::Printf(TEXT("DA_ES_Normal_%d"), Tier);
		const FString AssetPath = FString::Printf(
			TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/Normal/%s.%s"),
			*AssetName,
			*AssetName);
		UEnemyShipArchetypeData* NormalArchetype = LoadObject<UEnemyShipArchetypeData>(nullptr, *AssetPath);
		if (TestNotNull(*FString::Printf(TEXT("Normal %d DA loads"), Tier), NormalArchetype))
		{
			TestEqual(*FString::Printf(TEXT("Normal %d DA row"), Tier), NormalArchetype->SpecRow.RowName, RowName);
			TestTrue(
				*FString::Printf(TEXT("Normal %d is cannon-only"), Tier),
				NormalArchetype->SkillModules.Num() == 1 && NormalArchetype->SkillModules[0] == CannonModule);
			TestTrue(TEXT("Normal tier has positive tracking speed"), NormalArchetype->CannonAimProfile.TrackableTargetSpeed > 0.0f);
			TestTrue(TEXT("Normal tier has positive projectile flight time"), NormalArchetype->CannonAimProfile.ProjectileFlightTime > 0.0f);
			TestTrue(TEXT("Normal archetype references the production stat table"), NormalArchetype->SpecRow.DataTable == StatTable);
		}
	}

	const TCHAR* SkillNames[] = {TEXT("Charge"), TEXT("Obstacle"), TEXT("TimeStop"), TEXT("Torpedo")};
	for (const TCHAR* SkillName : SkillNames)
	{
		const FString AssetPath = FString::Printf(
			TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/Elite/DA_ES_%s.DA_ES_%s"),
			SkillName,
			SkillName);
		UEnemyShipArchetypeData* SkillArchetype = LoadObject<UEnemyShipArchetypeData>(nullptr, *AssetPath);
		if (TestNotNull(*FString::Printf(TEXT("%s DA loads"), SkillName), SkillArchetype))
		{
			TestTrue(TEXT("Elite archetype references the production stat table"), SkillArchetype->SpecRow.DataTable == StatTable);
			TestNotNull(TEXT("Elite archetype resolves its configured balance row"), SkillArchetype->SpecRow.GetRow<FShipStatRow>(TEXT("Elite fleet authoring")));
			TestTrue(
				*FString::Printf(TEXT("%s includes cannon and its skill"), SkillName),
				SkillArchetype->SkillModules.Num() == 2 && SkillArchetype->SkillModules.Contains(CannonModule));
			TestEqual(*FString::Printf(TEXT("%s uses baseline trackable speed"), SkillName), SkillArchetype->CannonAimProfile.TrackableTargetSpeed, 1000.0f);
			TestEqual(*FString::Printf(TEXT("%s uses baseline flight time"), SkillName), SkillArchetype->CannonAimProfile.ProjectileFlightTime, 3.0f);
			FDataValidationContext Context;
			TestFalse(
				*FString::Printf(TEXT("%s DA validates"), SkillName),
				SkillArchetype->IsDataValid(Context) == EDataValidationResult::Invalid);
		}
	}
	return true;
}

#endif
