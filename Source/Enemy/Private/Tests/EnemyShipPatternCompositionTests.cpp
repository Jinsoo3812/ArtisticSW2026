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
#include "ShipAI/Abilities/GA_EnemyShipCharge.h"
#include "ShipAI/Abilities/GA_EnemyShipLaunchTorpedo.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipAbilitySet.h"
#include "ShipAI/EnemyShipArchetypeData.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ShipAI/EnemyShipPatternData.h"
#include "ShipAI/EnemyShipPatternRuntimeComponent.h"

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
		const FGameplayTag AbilityTag,
		const TSubclassOf<UGameplayAbility> AbilityClass,
		const int32 Priority,
		const EEnemyShipSkillMovementPolicy MovementPolicy)
	{
		FEnemyShipSkillRule Rule;
		Rule.AbilityTag = AbilityTag;
		Rule.AbilityClass = AbilityClass;
		Rule.MinimumInterval = 0.0f;
		Rule.MinimumDistance = 0.0f;
		Rule.MaximumDistance = 5000.0f;
		Rule.Priority = Priority;
		Rule.MovementPolicy = MovementPolicy;
		return Rule;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipPatternCompositionMatrixTest,
	"ArtisticSW.Enemy.Ship.Pattern.CompositionMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipPatternCompositionMatrixTest::RunTest(const FString& Parameters)
{
	using namespace EnemyShipPatternCompositionTests;
	const FGameplayTag ChargeAbilityTag = GameplayAbility_EnemyShip_Charge;
	const FGameplayTag TorpedoAbilityTag = GameplayAbility_EnemyShip_LaunchTorpedo;

	FTestWorld TestWorld;
	AEnemyShip* Ship = TestWorld.World->SpawnActor<AEnemyShip>(
		AEnemyShip::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	AShip* PlayerShip = TestWorld.World->SpawnActor<AShip>(
		AShip::StaticClass(), FVector(1000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	ACannon* Cannon = TestWorld.World->SpawnActor<ACannon>(
		ACannon::StaticClass(), FVector(0.0f, 300.0f, 100.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Enemy Ship spawned"), Ship)
		|| !TestNotNull(TEXT("Player Ship spawned"), PlayerShip)
		|| !TestNotNull(TEXT("Mounted Cannon spawned"), Cannon))
	{
		return false;
	}

	Ship->BuoyancyRoot->SetSimulatePhysics(false);
	PlayerShip->BuoyancyRoot->SetSimulatePhysics(false);
	Cannon->AttachToActor(Ship, FAttachmentTransformRules::KeepWorldTransform);
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

	UEnemyShipAbilitySet* CommonAbilitySet = NewObject<UEnemyShipAbilitySet>();
	CommonAbilitySet->Abilities = {
		UGA_EnemyShipCharge::StaticClass(),
		UGA_EnemyShipLaunchTorpedo::StaticClass()
	};

	UEnemyShipPatternData* ChargeOnly = NewObject<UEnemyShipPatternData>();
	ChargeOnly->NavigationProfile.IdealDistance = 1200.0f;
	ChargeOnly->SkillRules.Add(MakeRule(
		ChargeAbilityTag,
		UGA_EnemyShipCharge::StaticClass(),
		100,
		EEnemyShipSkillMovementPolicy::OverrideNavigation));

	UEnemyShipPatternData* TorpedoOnly = NewObject<UEnemyShipPatternData>();
	TorpedoOnly->NavigationProfile.IdealDistance = 3200.0f;
	TorpedoOnly->SkillRules.Add(MakeRule(
		TorpedoAbilityTag,
		UGA_EnemyShipLaunchTorpedo::StaticClass(),
		100,
		EEnemyShipSkillMovementPolicy::ContinueNavigation));

	UEnemyShipPatternData* Mixed = NewObject<UEnemyShipPatternData>();
	Mixed->SelectionPolicy = EEnemyShipPatternSelectionPolicy::HighestPriority;
	Mixed->NavigationProfile.IdealDistance = 2200.0f;
	Mixed->SkillRules.Add(MakeRule(
		ChargeAbilityTag,
		UGA_EnemyShipCharge::StaticClass(),
		100,
		EEnemyShipSkillMovementPolicy::OverrideNavigation));
	Mixed->SkillRules.Add(MakeRule(
		TorpedoAbilityTag,
		UGA_EnemyShipLaunchTorpedo::StaticClass(),
		50,
		EEnemyShipSkillMovementPolicy::ContinueNavigation));

	UEnemyShipArchetypeData* Archetype = NewObject<UEnemyShipArchetypeData>();
	Archetype->AbilitySet = CommonAbilitySet;
	Archetype->Pattern = ChargeOnly;
	TestTrue(TEXT("Charge-only Archetype applies"), Archetype->ApplyToShip(Ship));

	UEnemyShipPatternRuntimeComponent* Runtime = Ship->GetPatternRuntimeComponent();
	FEnemyShipAbilitySelection Selection;
	TestTrue(TEXT("Charge-only Pattern selects"), Runtime->SelectAbilityAtTime(PlayerShip, 0.0, Selection));
	TestEqual(TEXT("Charge-only Pattern selects Charge"), Selection.AbilityTag, ChargeAbilityTag);
	TestEqual(
		TEXT("Charge-only Pattern applies its navigation profile"),
		Ship->GetNavigationComponent()->GetNavigationProfile().IdealDistance,
		1200.0f);

	const float HealthBeforePatternSwap = ASC->GetNumericAttribute(UBaseAttributeSet::GetMaxHealthAttribute());
	const float DamageBeforePatternSwap = ASC->GetNumericAttribute(UShipAttributeSet::GetCannonDamageAttribute());
	Archetype->Pattern = TorpedoOnly;
	TestTrue(TEXT("Torpedo-only Archetype applies to the same Enemy Ship"), Archetype->ApplyToShip(Ship));
	TestTrue(TEXT("Torpedo-only Pattern selects"), Runtime->SelectAbilityAtTime(PlayerShip, 0.0, Selection));
	TestEqual(TEXT("Torpedo-only Pattern selects Torpedo"), Selection.AbilityTag, TorpedoAbilityTag);
	TestEqual(
		TEXT("Torpedo-only Pattern applies its navigation profile"),
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

	Archetype->Pattern = Mixed;
	TestTrue(TEXT("Mixed Archetype applies to the same Enemy Ship"), Archetype->ApplyToShip(Ship));
	TestTrue(TEXT("Mixed Pattern initially selects"), Runtime->SelectAbilityAtTime(PlayerShip, 0.0, Selection));
	TestEqual(TEXT("Mixed Pattern initially prefers Charge"), Selection.AbilityTag, ChargeAbilityTag);

	FGameplayTagContainer ChargeTags(ChargeAbilityTag);
	TestTrue(TEXT("Selected Charge activates"), ASC->TryActivateAbilitiesByTag(ChargeTags, false));
	TestTrue(TEXT("Activated Charge selection commits"), Runtime->CommitSelection(Selection));
	ASC->CancelAbilities(&ChargeTags);
	TestTrue(TEXT("Charge remains on its independent GAS cooldown"), ASC->HasMatchingGameplayTag(Cooldown_EnemyShip_Charge));

	TestTrue(TEXT("Mixed Pattern can re-evaluate while Charge is cooling down"), Runtime->SelectAbilityAtTime(PlayerShip, 0.1, Selection));
	TestEqual(
		TEXT("Mixed Pattern skips cooling-down Charge and selects Torpedo"),
		Selection.AbilityTag,
		TorpedoAbilityTag);
	FGameplayTagContainer TorpedoTags(TorpedoAbilityTag);
	TestTrue(TEXT("Selected Torpedo activates"), ASC->TryActivateAbilitiesByTag(TorpedoTags, false));
	TestTrue(TEXT("Activated Torpedo selection commits"), Runtime->CommitSelection(Selection));
	TestTrue(TEXT("Torpedo owns its independent GAS cooldown"), ASC->HasMatchingGameplayTag(Cooldown_EnemyShip_LaunchTorpedo));

	TestFalse(
		TEXT("Mixed Pattern reports no candidate while every granted skill is unavailable"),
		Runtime->SelectAbilityAtTime(PlayerShip, 0.2, Selection));

	AEnemyShipTorpedo* SpawnedTorpedo = nullptr;
	for (TActorIterator<AEnemyShipTorpedo> It(TestWorld.World); It; ++It)
	{
		SpawnedTorpedo = *It;
		break;
	}
	TestNotNull(TEXT("Mixed Pattern reaches the real Torpedo GA spawn path"), SpawnedTorpedo);
	return true;
}

#endif
