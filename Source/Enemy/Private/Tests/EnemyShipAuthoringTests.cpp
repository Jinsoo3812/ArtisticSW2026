#if WITH_DEV_AUTOMATION_TESTS

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
#include "Misc/DataValidation.h"
#include "ShipAI/Abilities/GA_EnemyShipCannonVolley.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipArchetypeData.h"
#include "ShipAI/EnemyShipSkillModuleData.h"
#include "Ship.h"

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
	}
	TestEqual(TEXT("Enemy ship Blueprint authors exactly two cannons"), CannonCount, 2);
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
		TEXT("/Game/Blueprints/Ship/Data/DT_ShipStat.DT_ShipStat"));
	if (!TestNotNull(TEXT("DT_ShipStat loads"), StatTable))
	{
		return false;
	}

	const int32 ExpectedHealth[] = {100, 150, 225, 338};
	const int32 ExpectedDamage[] = {20, 30, 45, 68};
	const int32 ExpectedForward[] = {2, 3, 5, 7};
	const int32 ExpectedTurn[] = {1, 2, 2, 3};
	const float ExpectedCooldown[] = {4.0f, 4.0f / 1.5f, 4.0f / 2.25f, 4.0f / 3.375f};
	const float ExpectedTrackableSpeed[] = {1000.0f, 1500.0f, 2250.0f, 3375.0f};
	const float ExpectedFlightTime[] = {3.0f, 2.5f, 2.0f, 1.5f};
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
		TestEqual(*FString::Printf(TEXT("Normal %d health"), Tier), Row->MaxHealth, static_cast<float>(ExpectedHealth[Index]));
		TestEqual(*FString::Printf(TEXT("Normal %d damage"), Tier), Row->CannonDamage, static_cast<float>(ExpectedDamage[Index]));
		TestEqual(*FString::Printf(TEXT("Normal %d propulsion"), Tier), Row->ForwardPropulsionMultiplier, static_cast<float>(ExpectedForward[Index]));
		TestEqual(*FString::Printf(TEXT("Normal %d turn"), Tier), Row->TurnTorqueMultiplier, static_cast<float>(ExpectedTurn[Index]));
		TestTrue(
			*FString::Printf(TEXT("Normal %d fractional cannon cooldown"), Tier),
			FMath::IsNearlyEqual(Row->CannonFireCooldown, ExpectedCooldown[Index], 0.001f));

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
			TestEqual(*FString::Printf(TEXT("Normal %d trackable speed"), Tier), NormalArchetype->CannonAimProfile.TrackableTargetSpeed, ExpectedTrackableSpeed[Index]);
			TestEqual(*FString::Printf(TEXT("Normal %d projectile flight time"), Tier), NormalArchetype->CannonAimProfile.ProjectileFlightTime, ExpectedFlightTime[Index]);
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
			TestEqual(*FString::Printf(TEXT("%s uses baseline stats"), SkillName), SkillArchetype->SpecRow.RowName, FName(TEXT("EnemyShip_Easy")));
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
