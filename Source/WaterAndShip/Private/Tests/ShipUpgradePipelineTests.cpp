#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Ship.h"
#include "ShipAttributeSet.h"
#include "Upgrade/ShipUpgradeComponent.h"
#include "Upgrade/ShipUpgradeTreeDataAsset.h"

namespace ShipUpgradeTests
{
	FShipUpgradeNodeDefinition MakeNode(
		FName NodeId,
		EShipStatType StatType,
		EShipStatModifierOperation Operation,
		float Value,
		TArray<FName> Prerequisites = {})
	{
		FShipUpgradeNodeDefinition Node;
		Node.NodeId = NodeId;
		Node.DisplayName = FText::FromName(NodeId);
		Node.Description = FText::FromString(TEXT("Automation use-case node"));
		Node.PrerequisiteNodeIds = MoveTemp(Prerequisites);
		FShipStatModifier Modifier;
		Modifier.StatType = StatType;
		Modifier.Operation = Operation;
		Modifier.Value = Value;
		Node.StatModifiers.Add(Modifier);
		return Node;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShipUpgradeCalculationTest,
	"ArtisticSW.ShipUpgrade.CalculationAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipUpgradeCalculationTest::RunTest(const FString& Parameters)
{
	FShipStatSnapshot BaseStats;
	BaseStats.CannonDamage = 20.0f;
	BaseStats.CannonFireCooldownSeconds = 2.0f;
	BaseStats.ForwardPropulsionMultiplier = 1.0f;

	TArray<FShipUpgradeNodeDefinition> Nodes;
	Nodes.Add(ShipUpgradeTests::MakeNode(TEXT("Root_Damage"), EShipStatType::CannonDamage, EShipStatModifierOperation::AddFlat, 10.0f));
	Nodes.Add(ShipUpgradeTests::MakeNode(TEXT("Child_Cooldown"), EShipStatType::CannonFireCooldown, EShipStatModifierOperation::AddFlat, -0.5f, { TEXT("Root_Damage") }));
	Nodes.Add(ShipUpgradeTests::MakeNode(TEXT("Child_Propulsion"), EShipStatType::ForwardPropulsion, EShipStatModifierOperation::AddPercent, 0.2f, { TEXT("Root_Damage") }));

	const FShipStatSnapshot RootOnly = FShipUpgradeCalculator::Calculate(BaseStats, Nodes, { TEXT("Root_Damage"), TEXT("Root_Damage") });
	TestEqual(TEXT("Duplicate active IDs apply a node once"), RootOnly.CannonDamage, 30.0f);
	TestEqual(TEXT("Unselected cooldown node changes nothing"), RootOnly.CannonFireCooldownSeconds, 2.0f);

	const FShipStatSnapshot Full = FShipUpgradeCalculator::Calculate(BaseStats, Nodes, { TEXT("Root_Damage"), TEXT("Child_Cooldown"), TEXT("Child_Propulsion") });
	TestEqual(TEXT("Flat cooldown reduction is applied"), Full.CannonFireCooldownSeconds, 1.5f);
	TestEqual(TEXT("Percent propulsion increase is applied independently"), Full.ForwardPropulsionMultiplier, 1.2f);
	TestEqual(TEXT("Turn multiplier remains independent"), Full.TurnTorqueMultiplier, 1.0f);

	UShipUpgradeTreeDataAsset* Tree = NewObject<UShipUpgradeTreeDataAsset>();
	Tree->Nodes = Nodes;
	TArray<FText> Errors;
	TestTrue(TEXT("Valid node graph passes validation"), Tree->ValidateTree(Errors));
	FCraftingItemStack InvalidCost;
	InvalidCost.Quantity = 1;
	Tree->Nodes[0].ActivationCosts.Add(InvalidCost);
	TestFalse(TEXT("Invalid material tag fails tree validation"), Tree->ValidateTree(Errors));
	Tree->Nodes[0].ActivationCosts.Reset();
	Tree->Nodes[0].PrerequisiteNodeIds.Add(TEXT("Child_Cooldown"));
	TestFalse(TEXT("Cyclic node graph fails validation"), Tree->ValidateTree(Errors));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShipUpgradeFullPipelineTest,
	"ArtisticSW.ShipUpgrade.FullPipelineUseCase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipUpgradeFullPipelineTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ShipUpgradePipelineTestWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), World)) return false;
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	APlayerState* PlayerState = World->SpawnActor<APlayerState>();
	TestNotNull(TEXT("Authoritative player state is spawned"), PlayerState);
	UShipUpgradeComponent* UpgradeComponent = NewObject<UShipUpgradeComponent>(PlayerState, TEXT("UseCaseShipUpgrade"));
	PlayerState->AddInstanceComponent(UpgradeComponent);
	UpgradeComponent->RegisterComponent();

	UShipUpgradeTreeDataAsset* Tree = NewObject<UShipUpgradeTreeDataAsset>();
	Tree->Nodes.Add(ShipUpgradeTests::MakeNode(TEXT("Hull_I"), EShipStatType::MaxHealth, EShipStatModifierOperation::AddFlat, 50.0f));
	Tree->Nodes.Add(ShipUpgradeTests::MakeNode(TEXT("Cannon_I"), EShipStatType::CannonDamage, EShipStatModifierOperation::AddFlat, 10.0f, { TEXT("Hull_I") }));
	Tree->Nodes.Add(ShipUpgradeTests::MakeNode(TEXT("Reload_I"), EShipStatType::CannonFireCooldown, EShipStatModifierOperation::AddFlat, -0.25f, { TEXT("Cannon_I") }));

	FShipStatSnapshot BaseStats;
	BaseStats.MaxHealth = 100.0f;
	BaseStats.CannonDamage = 20.0f;
	BaseStats.CannonFireCooldownSeconds = 2.0f;
	UpgradeComponent->ConfigureForUseCase(Tree, BaseStats, false);

	TestEqual(TEXT("Root node is initially available"), UpgradeComponent->GetNodeState(TEXT("Hull_I")), EShipUpgradeNodeState::Available);
	TestEqual(TEXT("Child node is initially locked"), UpgradeComponent->GetNodeState(TEXT("Cannon_I")), EShipUpgradeNodeState::Locked);
	TestEqual(TEXT("Locked child activation is rejected"), UpgradeComponent->ActivateNodeForUseCase(TEXT("Cannon_I")), EShipUpgradeActivationResult::MissingPrerequisite);

	FShipStatSnapshot Preview;
	TestTrue(TEXT("Available root can be previewed"), UpgradeComponent->GetStatsAfterActivating(TEXT("Hull_I"), Preview));
	TestEqual(TEXT("Preview reports upgraded health"), Preview.MaxHealth, 150.0f);
	TestEqual(TEXT("Root activation succeeds"), UpgradeComponent->ActivateNodeForUseCase(TEXT("Hull_I")), EShipUpgradeActivationResult::Success);
	TestEqual(TEXT("Root activation is idempotent"), UpgradeComponent->ActivateNodeForUseCase(TEXT("Hull_I")), EShipUpgradeActivationResult::AlreadyActive);
	TestEqual(TEXT("First child unlocks"), UpgradeComponent->GetNodeState(TEXT("Cannon_I")), EShipUpgradeNodeState::Available);
	TestEqual(TEXT("First child activates"), UpgradeComponent->ActivateNodeForUseCase(TEXT("Cannon_I")), EShipUpgradeActivationResult::Success);
	TestEqual(TEXT("Second child activates after its prerequisite"), UpgradeComponent->ActivateNodeForUseCase(TEXT("Reload_I")), EShipUpgradeActivationResult::Success);

	const FShipStatSnapshot FinalStats = UpgradeComponent->GetCurrentShipStats();
	TestEqual(TEXT("Pipeline final health"), FinalStats.MaxHealth, 150.0f);
	TestEqual(TEXT("Pipeline final cannon damage"), FinalStats.CannonDamage, 30.0f);
	TestEqual(TEXT("Pipeline final cannon cooldown"), FinalStats.CannonFireCooldownSeconds, 1.75f);
	TestEqual(TEXT("UI returns all designer nodes"), UpgradeComponent->GetAllNodeViews().Num(), 3);
	TestEqual(TEXT("UI returns the active node's stat change"), UpgradeComponent->GetNodeStatChanges(TEXT("Cannon_I")).Num(), 1);

	const FString PersistenceSlot = FString::Printf(TEXT("ShipUpgradeAutomation_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UpgradeComponent->SaveSlotName = PersistenceSlot;
	TestTrue(TEXT("Active NodeIds save to persistent progress"), UpgradeComponent->SaveProgress());
	UShipUpgradeComponent* ReloadedComponent = NewObject<UShipUpgradeComponent>(PlayerState, TEXT("ReloadedShipUpgrade"));
	PlayerState->AddInstanceComponent(ReloadedComponent);
	ReloadedComponent->RegisterComponent();
	ReloadedComponent->ConfigureForUseCase(Tree, BaseStats, false);
	ReloadedComponent->SaveSlotName = PersistenceSlot;
	TestTrue(TEXT("Persistent progress reload succeeds"), ReloadedComponent->LoadProgress());
	TestEqual(TEXT("Reload restores all active NodeIds"), ReloadedComponent->GetActiveNodeIds().Num(), 3);
	TestTrue(TEXT("Reloaded progress reproduces final stats"), ReloadedComponent->GetCurrentShipStats().Equals(FinalStats));

	UDataTable* ShipTable = NewObject<UDataTable>();
	ShipTable->RowStruct = FShipStatRow::StaticStruct();
	FShipStatRow PlayerRow;
	PlayerRow.MaxHealth = BaseStats.MaxHealth;
	PlayerRow.CannonDamage = BaseStats.CannonDamage;
	PlayerRow.CannonFireCooldown = BaseStats.CannonFireCooldownSeconds;
	PlayerRow.ForwardPropulsionMultiplier = 1.0f;
	PlayerRow.TurnTorqueMultiplier = 1.0f;
	ShipTable->AddRow(TEXT("PlayerShip"), PlayerRow);

	AShip* Ship = World->SpawnActor<AShip>();
	TestNotNull(TEXT("Player ship is spawned"), Ship);
	if (Ship)
	{
		Ship->ShipStatTable = ShipTable;
		Ship->ShipStatRowName = TEXT("PlayerShip");
		// The transient test world does not run the full map actor initialization path,
		// so mirror component registration and AShip::BeginPlay's GAS initialization.
		Ship->GetAbilitySystemComponent()->AddSpawnedAttribute(Ship->GetShipAttributeSet());
		Ship->GetAbilitySystemComponent()->InitAbilityActorInfo(Ship, Ship);
		TestTrue(TEXT("Player upgrades apply through PlayerState to Ship"), Ship->ApplyPlayerUpgrades(PlayerState));
		UAbilitySystemComponent* ShipASC = Ship->GetAbilitySystemComponent();
		TestNotNull(TEXT("Ship ability system is available"), ShipASC);
		if (ShipASC)
		{
			TestEqual(TEXT("Ship receives upgraded max health"), ShipASC->GetNumericAttribute(UShipAttributeSet::GetMaxHealthAttribute()), 150.0f);
			TestEqual(TEXT("Ship receives upgraded cannon damage"), ShipASC->GetNumericAttribute(UShipAttributeSet::GetCannonDamageAttribute()), 30.0f);
			TestEqual(TEXT("Ship receives upgraded cooldown"), ShipASC->GetNumericAttribute(UShipAttributeSet::GetCannonFireCooldownAttribute()), 1.75f);
		}
	}

	UGameplayStatics::DeleteGameInSlot(FString::Printf(TEXT("%s_%d"), *PersistenceSlot, PlayerState->GetPlayerId()), 0);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
