#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "Cannon.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Ship.h"
#include "ShipAttributeSet.h"
#include "ShipAI/Abilities/EnemyShipTorpedo.h"
#include "ShipAI/Abilities/GA_EnemyShipCannonVolley.h"
#include "ShipAI/Abilities/GA_EnemyShipCharge.h"
#include "ShipAI/Abilities/GA_EnemyShipLaunchTorpedo.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipAbilitySet.h"
#include "ShipAI/EnemyShipArchetypeData.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ShipAI/EnemyShipPatternData.h"
#include "ShipAI/EnemyShipPatternRuntimeComponent.h"
#include "ShipAI/EnemyShipSkillModuleData.h"

namespace EnemyShipPatternCompositionTests
{
	struct FTestWorld
	{
		UWorld* World = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("EnemyShipPatternCompositionWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}
	};

	FEnemyShipSkillRule MakeRule(
		const FName RuleId,
		const FGameplayTag AbilityTag,
		const TSubclassOf<UGameplayAbility> AbilityClass,
		const int32 Priority,
		const EEnemyShipSkillMovementPolicy MovementPolicy)
	{
		FEnemyShipSkillRule Rule;
		Rule.RuleId = RuleId;
		Rule.AbilityTag = AbilityTag;
		Rule.AbilityClass = AbilityClass;
		Rule.MinimumInterval = 0.0f;
		Rule.Priority = Priority;
		Rule.MovementPolicy = MovementPolicy;
		return Rule;
	}

	UEnemyShipSkillModuleData* MakeModule(
		const FName ModuleId,
		const FEnemyShipSkillRule& Rule,
		const TSubclassOf<UGameplayAbility> AbilityClass)
	{
		UEnemyShipAbilitySet* AbilitySet = NewObject<UEnemyShipAbilitySet>();
		AbilitySet->Abilities.Add(AbilityClass);
		UEnemyShipSkillModuleData* Module = NewObject<UEnemyShipSkillModuleData>();
		Module->ModuleId = ModuleId;
		Module->AbilitySet = AbilitySet;
		Module->SkillRules.Add(Rule);
		return Module;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipPatternCompositionMatrixTest,
	"ArtisticSW.Enemy.Ship.Pattern.CompositionMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipPatternCompositionMatrixTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("QuestItem"), EAutomationExpectedErrorFlags::Contains, 3);

	using namespace EnemyShipPatternCompositionTests;
	const FGameplayTag ChargeAbilityTag = GameplayAbility_EnemyShip_Charge;
	const FGameplayTag TorpedoAbilityTag = GameplayAbility_EnemyShip_LaunchTorpedo;
	const FGameplayTag CannonAbilityTag = GameplayAbility_EnemyShip_CannonVolley;

	FTestWorld TestWorld;
	AEnemyShip* Ship = TestWorld.World->SpawnActor<AEnemyShip>(
		AEnemyShip::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	AShip* PlayerShip = TestWorld.World->SpawnActor<AShip>(
		AShip::StaticClass(), FVector(1000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	ACannon* NearCannon = TestWorld.World->SpawnActor<ACannon>(
		ACannon::StaticClass(), FVector(0.0f, 100.0f, 100.0f), FRotator::ZeroRotator);
	ACannon* MiddleCannon = TestWorld.World->SpawnActor<ACannon>(
		ACannon::StaticClass(), FVector(0.0f, 200.0f, 100.0f), FRotator::ZeroRotator);
	ACannon* FarCannon = TestWorld.World->SpawnActor<ACannon>(
		ACannon::StaticClass(), FVector(0.0f, 300.0f, 100.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Enemy Ship spawned"), Ship)
		|| !TestNotNull(TEXT("Player Ship spawned"), PlayerShip)
		|| !TestNotNull(TEXT("Near Cannon spawned"), NearCannon)
		|| !TestNotNull(TEXT("Middle Cannon spawned"), MiddleCannon)
		|| !TestNotNull(TEXT("Far Cannon spawned"), FarCannon))
	{
		return false;
	}

	Ship->BuoyancyRoot->SetSimulatePhysics(false);
	PlayerShip->BuoyancyRoot->SetSimulatePhysics(false);
	NearCannon->AttachToActor(Ship, FAttachmentTransformRules::KeepWorldTransform);
	MiddleCannon->AttachToActor(Ship, FAttachmentTransformRules::KeepWorldTransform);
	FarCannon->AttachToActor(Ship, FAttachmentTransformRules::KeepWorldTransform);
	Ship->RefreshMountedCannons();
	Ship->GetNavigationComponent()->SetTargetShip(PlayerShip);

	FShipStatSnapshot Stats;
	Stats.MaxHealth = 600.0f;
	Stats.CannonDamage = 40.0f;
	Stats.CannonFireCooldownSeconds = 30.0f;
	Stats.CannonballSpeed = 3000.0f;
	Stats.ForwardPropulsionMultiplier = 1.5f;
	Stats.TurnTorqueMultiplier = 1.0f;
	Ship->ApplyStatSnapshot(Stats, true);

	UAbilitySystemComponent* ASC = Ship->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("Enemy ASC exists"), ASC))
	{
		return false;
	}
	ASC->InitAbilityActorInfo(Ship, Ship);

	UEnemyShipSkillModuleData* CannonModule = MakeModule(
		TEXT("Core.CannonVolley"),
		MakeRule(
			TEXT("Core.CannonVolley.Fire"),
			CannonAbilityTag,
			UGA_EnemyShipCannonVolley::StaticClass(),
			10,
			EEnemyShipSkillMovementPolicy::ContinueNavigation),
		UGA_EnemyShipCannonVolley::StaticClass());
	UEnemyShipSkillModuleData* ChargeModule = MakeModule(
		TEXT("Skill.Charge"),
		MakeRule(
			TEXT("Skill.Charge.Use"),
		ChargeAbilityTag,
		UGA_EnemyShipCharge::StaticClass(),
		100,
			EEnemyShipSkillMovementPolicy::OverrideNavigation),
		UGA_EnemyShipCharge::StaticClass());
	UEnemyShipSkillModuleData* TorpedoModule = MakeModule(
		TEXT("Skill.Torpedo"),
		MakeRule(
			TEXT("Skill.Torpedo.Use"),
		TorpedoAbilityTag,
		UGA_EnemyShipLaunchTorpedo::StaticClass(),
		50,
			EEnemyShipSkillMovementPolicy::ContinueNavigation),
		UGA_EnemyShipLaunchTorpedo::StaticClass());
	Ship->SetCoreSkillModules({CannonModule});

	UEnemyShipPatternData* Basic = NewObject<UEnemyShipPatternData>();
	Basic->NavigationProfile.IdealDistance = 4000.0f;
	UEnemyShipPatternData* Rammer = NewObject<UEnemyShipPatternData>();
	Rammer->NavigationProfile.IdealDistance = 1200.0f;
	Rammer->SkillModules.Add(ChargeModule);
	UEnemyShipPatternData* TorpedoBoat = NewObject<UEnemyShipPatternData>();
	TorpedoBoat->NavigationProfile.IdealDistance = 3200.0f;
	TorpedoBoat->SkillModules.Add(TorpedoModule);
	UEnemyShipPatternData* Elite = NewObject<UEnemyShipPatternData>();
	Elite->SelectionPolicy = EEnemyShipPatternSelectionPolicy::HighestPriority;
	Elite->NavigationProfile.IdealDistance = 2200.0f;
	// Authoring mistakes that repeat a Core module must not double-grant or double-schedule it.
	Elite->SkillModules = {CannonModule, ChargeModule, TorpedoModule};

	UEnemyShipArchetypeData* Archetype = NewObject<UEnemyShipArchetypeData>();
	Archetype->Pattern = Basic;
	TestTrue(TEXT("Basic Archetype applies"), Archetype->ApplyToShip(Ship));

	UEnemyShipPatternRuntimeComponent* Runtime = Ship->GetPatternRuntimeComponent();
	FEnemyShipAbilitySelection Selection;
	TestEqual(TEXT("Basic Pattern resolves only the Core Rule"), Runtime->GetResolvedRuleCount(), 1);
	TestTrue(TEXT("Basic Pattern selects"), Runtime->SelectAbilityAtTime(PlayerShip, 0.0, Selection));
	TestEqual(TEXT("Basic Pattern selects only CannonVolley"), Selection.AbilityTag, CannonAbilityTag);

	Archetype->Pattern = Rammer;
	TestTrue(TEXT("Rammer Archetype applies"), Archetype->ApplyToShip(Ship));
	TestEqual(TEXT("Rammer composes Core plus Charge"), Runtime->GetResolvedRuleCount(), 2);
	TestTrue(TEXT("Rammer Pattern selects"), Runtime->SelectAbilityAtTime(PlayerShip, 0.0, Selection));
	TestEqual(TEXT("Rammer Pattern selects Charge"), Selection.AbilityTag, ChargeAbilityTag);
	TestEqual(
		TEXT("Rammer Pattern applies its navigation profile"),
		Ship->GetNavigationComponent()->GetNavigationProfile().IdealDistance,
		1200.0f);

	const float HealthBeforePatternSwap = ASC->GetNumericAttribute(UBaseAttributeSet::GetMaxHealthAttribute());
	const float DamageBeforePatternSwap = ASC->GetNumericAttribute(UShipAttributeSet::GetCannonDamageAttribute());
	Archetype->Pattern = TorpedoBoat;
	TestTrue(TEXT("TorpedoBoat Archetype applies to the same Enemy Ship"), Archetype->ApplyToShip(Ship));
	TestTrue(TEXT("TorpedoBoat Pattern selects"), Runtime->SelectAbilityAtTime(PlayerShip, 0.0, Selection));
	TestEqual(TEXT("TorpedoBoat Pattern selects Torpedo"), Selection.AbilityTag, TorpedoAbilityTag);
	TestEqual(
		TEXT("TorpedoBoat Pattern applies its navigation profile"),
		Ship->GetNavigationComponent()->GetNavigationProfile().IdealDistance,
		3200.0f);
	TestEqual(
		TEXT("Pattern swap does not change MaxHealth"),
		ASC->GetNumericAttribute(UBaseAttributeSet::GetMaxHealthAttribute()),
		HealthBeforePatternSwap);
	TestEqual(
		TEXT("Pattern swap does not change CannonDamage"),
		ASC->GetNumericAttribute(UShipAttributeSet::GetCannonDamageAttribute()),
		DamageBeforePatternSwap);

	Archetype->Pattern = Elite;
	TestTrue(TEXT("Elite Archetype applies to the same Enemy Ship"), Archetype->ApplyToShip(Ship));
	TestEqual(TEXT("Elite composes Core, Charge, and Torpedo"), Runtime->GetResolvedRuleCount(), 3);
	TestTrue(TEXT("Elite Pattern initially selects"), Runtime->SelectAbilityAtTime(PlayerShip, 0.0, Selection));
	TestEqual(TEXT("Elite Pattern initially prefers Charge"), Selection.AbilityTag, ChargeAbilityTag);

	FGameplayTagContainer ChargeTags(ChargeAbilityTag);
	TestTrue(TEXT("Selected Charge activates"), ASC->TryActivateAbilitiesByTag(ChargeTags, false));
	TestTrue(TEXT("Activated Charge selection commits"), Runtime->CommitSelection(Selection));
	ASC->CancelAbilities(&ChargeTags);
	TestTrue(TEXT("Charge remains on its independent GAS cooldown"), ASC->HasMatchingGameplayTag(Cooldown_EnemyShip_Charge));

	TestTrue(TEXT("Elite Pattern can re-evaluate while Charge is cooling down"), Runtime->SelectAbilityAtTime(PlayerShip, 0.1, Selection));
	TestEqual(
		TEXT("Elite Pattern skips cooling-down Charge and selects Torpedo"),
		Selection.AbilityTag,
		TorpedoAbilityTag);
	FGameplayTagContainer TorpedoTags(TorpedoAbilityTag);
	TestTrue(TEXT("Selected Torpedo activates"), ASC->TryActivateAbilitiesByTag(TorpedoTags, false));
	TestTrue(TEXT("Activated Torpedo selection commits"), Runtime->CommitSelection(Selection));
	TestTrue(TEXT("Torpedo owns its independent GAS cooldown"), ASC->HasMatchingGameplayTag(Cooldown_EnemyShip_LaunchTorpedo));

	TestTrue(TEXT("Elite falls back to Core Cannon while both special skills cool down"),
		Runtime->SelectAbilityAtTime(PlayerShip, 0.2, Selection));
	TestEqual(TEXT("Core fallback is CannonVolley"), Selection.AbilityTag, CannonAbilityTag);
	FGameplayTagContainer CannonTags(CannonAbilityTag);
	AddExpectedError(TEXT("projectile class is null"), EAutomationExpectedErrorFlags::Contains, 3);
	TestTrue(TEXT("Selected CannonVolley activates"), ASC->TryActivateAbilitiesByTag(CannonTags, false));
	TestTrue(TEXT("Activated CannonVolley selection commits"), Runtime->CommitSelection(Selection));
	TestFalse(TEXT("Volley consumes the nearest cannon reload"), NearCannon->CanFireCannon());
	TestFalse(TEXT("Volley consumes the second-nearest cannon reload"), MiddleCannon->CanFireCannon());
	TestTrue(TEXT("Volley respects MaxCannonsPerVolley and leaves the third cannon ready"), FarCannon->CanFireCannon());
	TestTrue(TEXT("Core Cannon remains selectable while another mounted cannon is ready"),
		Runtime->SelectAbilityAtTime(PlayerShip, 0.3, Selection));
	TestEqual(TEXT("Second Core selection is CannonVolley"), Selection.AbilityTag, CannonAbilityTag);
	TestTrue(TEXT("Second CannonVolley activates the remaining cannon"), ASC->TryActivateAbilitiesByTag(CannonTags, false));
	TestTrue(TEXT("Second CannonVolley selection commits"), Runtime->CommitSelection(Selection));
	TestFalse(TEXT("Remaining cannon now owns its reload"), FarCannon->CanFireCannon());
	TestFalse(TEXT("No candidate remains while specials cool down and the cannon reloads"),
		Runtime->SelectAbilityAtTime(PlayerShip, 0.4, Selection));

	AEnemyShipTorpedo* SpawnedTorpedo = nullptr;
	for (TActorIterator<AEnemyShipTorpedo> It(TestWorld.World); It; ++It)
	{
		SpawnedTorpedo = *It;
		break;
	}
	TestNotNull(TEXT("Elite Pattern reaches the real Torpedo GA spawn path"), SpawnedTorpedo);
	return true;
}

#endif
