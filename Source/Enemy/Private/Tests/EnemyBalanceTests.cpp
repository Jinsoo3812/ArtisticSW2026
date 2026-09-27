#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseEnemy.h"
#include "BaseGameplayTags.h"
#include "EnemyBalanceData.h"
#include "BossAI/ShipBossEnemy.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "Components/BaseHealthComponent.h"
#include "UObject/Script.h"
#include "GASDamageInstantGameplayEffect.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyBalanceCsvTest, "ArtisticSW.Enemy.Balance.Csv",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEnemyBalanceCsvTest::RunTest(const FString&)
{
	UDataTable* Combat = LoadObject<UDataTable>(nullptr, TEXT("/Game/GameplayAbilitySystem/Enemy/Data/DT_EnemyCombatBalance"));
	UDataTable* Stats = LoadObject<UDataTable>(nullptr, TEXT("/Game/GameplayAbilitySystem/Enemy/Data/DT_EnemyBaseStats"));
	if (!TestNotNull(TEXT("Imported combat DT exists"), Combat) || !TestNotNull(TEXT("Imported stats DT exists"), Stats)) return false;
	TestEqual(TEXT("All 24 archetypes imported"), Stats->GetRowMap().Num(), 24);
	TestEqual(TEXT("All 24 combat rows imported"), Combat->GetRowMap().Num(), 24);
	for (const auto& Pair : Stats->GetRowMap())
	{
		const auto& Row = *reinterpret_cast<const FEnemyBaseStatsRow*>(Pair.Value);
		TestTrue(*Pair.Key.ToString(), Row.IsValid());
		const auto* Cadence = Row.CombatSettings.GetRow<FEnemyCombatBalanceRow>(TEXT("Balance CSV test"));
		TestTrue(TEXT("Each archetype has valid cadence"), Cadence && Cadence->IsValid());
	}
	// Check native CSV import against the saved asset, including structured row references.
	FString Csv;
	TestTrue(TEXT("Versioned CSV readable"), FFileHelper::LoadFileToString(Csv, *(FPaths::ProjectDir() / TEXT("DataTable/EnemyBaseStats.csv"))));
	UDataTable* Imported = NewObject<UDataTable>();
	Imported->RowStruct = FEnemyBaseStatsRow::StaticStruct();
	const auto Errors = Imported->CreateTableFromCSVString(Csv);
	TestEqual(TEXT("CSV imports without warnings"), Errors.Num(), 0);
	for (const FName Name : Stats->GetRowNames())
	{
		const auto* Saved = Stats->FindRow<FEnemyBaseStatsRow>(Name, TEXT("Saved"));
		const auto* Source = Imported->FindRow<FEnemyBaseStatsRow>(Name, TEXT("Source"));
		if (TestNotNull(TEXT("Source row exists"), Source))
		{
			TestEqual(TEXT("Saved HP matches CSV"), Saved->MaxHealth, Source->MaxHealth);
			TestEqual(TEXT("Saved Strength matches CSV"), Saved->Strength, Source->Strength);
			TestTrue(TEXT("Combat handle matches CSV"), Saved->CombatSettings == Source->CombatSettings);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyBalanceInitializationTest, "ArtisticSW.Enemy.Balance.Initialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEnemyBalanceInitializationTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("EnemyBalanceTestWorld"));
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	ABaseEnemy* Enemy = World->SpawnActor<ABaseEnemy>();
	UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent();
	ASC->InitAbilityActorInfo(Enemy, Enemy);
	ASC->AddAttributeSetSubobject(Cast<UBaseAttributeSet>(Enemy->GetDefaultSubobjectByName(TEXT("BasicAttributeSet"))));
	UDataTable* Table = NewObject<UDataTable>();
	Table->RowStruct = FEnemyBaseStatsRow::StaticStruct();
	FEnemyBaseStatsRow First;
	First.MaxHealth = 50.f;
	First.Strength = 8.f;
	Table->AddRow(TEXT("First"), First);
	FEnemyBaseStatsRow Second = First;
	Second.MaxHealth = 150.f;
	Second.Strength = 23.f;
	Table->AddRow(TEXT("Second"), Second);
	FDataTableRowHandle Selection;
	Selection.DataTable = Table;
	Selection.RowName = TEXT("First");
	TestTrue(TEXT("Pre-spawn row selection"), Enemy->ConfigureSpawnBalance(Selection, 2.f, 1.f));
	TestTrue(TEXT("Initialize selected row"), Enemy->ApplyBaseStatsForSpawn());
	TestEqual(TEXT("Wave HP multiplier applied once"), ASC->GetNumericAttribute(UBaseAttributeSet::GetMaxHealthAttribute()), 100.f);
	TestEqual(TEXT("Initial strength"), ASC->GetNumericAttribute(UBaseAttributeSet::GetStrengthAttribute()), 8.f);
	ASC->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), 31.f);
	Enemy->ApplyBaseStatsForSpawn();
	TestEqual(TEXT("Repeated initialize cannot heal"), ASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()), 31.f);
	Selection.RowName = TEXT("Second");
	TestFalse(TEXT("Live reconfiguration rejected"), Enemy->ConfigureSpawnBalance(Selection));
	Enemy->ResetBalanceForReuse();
	TestTrue(TEXT("New lifecycle accepts another tier"), Enemy->ConfigureSpawnBalance(Selection));
	TestTrue(TEXT("Reused enemy initializes"), Enemy->ApplyBaseStatsForSpawn());
	TestEqual(TEXT("No previous HP multiplier remains"), ASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()), 150.f);
	TestEqual(TEXT("Strength replaced rather than added"), ASC->GetNumericAttribute(UBaseAttributeSet::GetStrengthAttribute()), 23.f);
	Enemy->ResetBalanceForReuse();
	TestFalse(TEXT("Reject invalid wave scaling"), Enemy->ConfigureSpawnBalance(Selection, -1.f));
	FEnemyBaseStatsRow Bad;
	Bad.MaxHealth = -1.f;
	Table->AddRow(TEXT("Bad"), Bad);
	Selection.RowName = TEXT("Bad");
	Enemy->ConfigureSpawnBalance(Selection);
	AddExpectedError(TEXT("[EnemyBalance] Invalid stats"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Invalid DT cannot initialize an enemy"), Enemy->ApplyBaseStatsForSpawn());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyBalanceHitCountTest, "ArtisticSW.Enemy.Balance.HitCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEnemyBalanceHitCountTest::RunTest(const FString&)
{
	UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/GameplayAbilitySystem/Enemy/Data/DT_EnemyBaseStats"));
	if (!TestNotNull(TEXT("Stats table"), Table)) return false;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("EnemyBalanceDamageWorld"));
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	ABaseEnemy* Enemy = World->SpawnActor<ABaseEnemy>();
	UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent();
	ASC->InitAbilityActorInfo(Enemy, Enemy);
	ASC->AddAttributeSetSubobject(Cast<UBaseAttributeSet>(Enemy->GetDefaultSubobjectByName(TEXT("BasicAttributeSet"))));
	const int32 BossHits[] = {12, 15, 18, 22};
	for (FName Name : Table->GetRowNames())
	{
		const auto* Row = Table->FindRow<FEnemyBaseStatsRow>(Name, TEXT("Hit count"));
		Enemy->ResetBalanceForReuse();
		FDataTableRowHandle Handle; Handle.DataTable = Table; Handle.RowName = Name;
		Enemy->ConfigureSpawnBalance(Handle);
		Enemy->ApplyBaseStatsForSpawn();
		const float Damage = 5.f + Row->Tier * 5.f;
		int32 Hits = 0;
		while (ASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()) > 0.f && Hits < 100)
		{
			auto Spec = ASC->MakeOutgoingSpec(UGASDamageInstantGameplayEffect::StaticClass(), 1.f, ASC->MakeEffectContext());
			Spec.Data->SetSetByCallerMagnitude(Data_Damage, Damage);
			ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
			++Hits;
		}
		const bool bMelee = Row->EnemyType == EEnemyBalanceType::GroundMelee || Row->EnemyType == EEnemyBalanceType::DeckMelee;
		const int32 Expected = Row->EnemyType == EEnemyBalanceType::Boss ? BossHits[Row->Tier - 1]
			: Row->EnemyType == EEnemyBalanceType::BossSummonRanged ? 4 : bMelee ? (Row->Tier < 3 ? 5 : 6) : (Row->Tier < 3 ? 4 : 5);
		TestEqual(*FString::Printf(TEXT("%s actual GAS damage count"), *Name.ToString()), Hits, Expected);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyBalanceSummonThresholdTest, "ArtisticSW.Enemy.Balance.SummonThresholds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEnemyBalanceSummonThresholdTest::RunTest(const FString&)
{
	FEditorScriptExecutionGuard ScriptGuard;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("EnemyBalanceSummonWorld"));
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	AShipBossEnemy* Boss = World->SpawnActor<AShipBossEnemy>();
	UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent();
	ASC->InitAbilityActorInfo(Boss, Boss);
	ASC->AddAttributeSetSubobject(Cast<UBaseAttributeSet>(Boss->GetDefaultSubobjectByName(TEXT("BasicAttributeSet"))));
	Boss->ApplyBaseStatsForSpawn();
	Boss->GetHealthComponent()->InitializeWithAbilitySystem(ASC);
	Boss->bUseEncounterBalance = true;
	Boss->EncounterBalance.SummonHealthFractions = { .6f, .3f };
	Boss->EncounterBalance.SummonCount = 2;
	Boss->MaxSummonedDeckEnemies = 2;
	Boss->GetHealthComponent()->OnHealthChanged.AddUniqueDynamic(Boss, &AShipBossEnemy::HandleBalanceHealthChanged);
	ASC->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), 60.f);
	TestEqual(TEXT("First threshold queues exactly one wave"), Boss->PendingBalanceSummons, 2);
	Boss->PendingBalanceSummons = 0; // Simulate consumption by the BT.
	ASC->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), 80.f);
	ASC->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), 50.f);
	TestEqual(TEXT("Healing cannot rearm threshold"), Boss->PendingBalanceSummons, 0);
	ASC->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), 30.f);
	TestEqual(TEXT("Second threshold queues its own wave"), Boss->PendingBalanceSummons, 2);
	Boss->PendingBalanceSummons = 0;
	Boss->ConsumedSummonThresholds.Reset();
	ASC->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), 100.f);
	ASC->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), 20.f);
	TestEqual(TEXT("Crossing both at once cannot exceed cap"), Boss->PendingBalanceSummons, 2);
	TestEqual(TEXT("Both crossed thresholds consumed"), Boss->ConsumedSummonThresholds.Num(), 2);
	Boss->PendingBalanceSummons = 0;
	Boss->ConsumedSummonThresholds.Reset();
	ASC->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), 100.f);
	// Lethal hit must not request adds, irrespective of threshold count.
	ASC->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), 0.f);
	TestEqual(TEXT("Death does not queue summons"), Boss->PendingBalanceSummons, 0);
	return true;
}
#endif
