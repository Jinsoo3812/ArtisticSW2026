#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "Cannon.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Ship.h"
#include "ShipAttributeSet.h"
#include "ShipAI/Abilities/EnemyShipSkillMath.h"
#include "ShipAI/Abilities/EnemyShipTorpedo.h"
#include "ShipAI/Abilities/GA_EnemyShipCharge.h"
#include "ShipAI/Abilities/GA_EnemyShipLaunchTorpedo.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipNavigationComponent.h"

namespace EnemyShipAbilityTests
{
	struct FTestWorld
	{
		UWorld* World = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("EnemyShipAbilityWorld"));
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

	const FGameplayAbilitySpec* FindAbilitySpec(
		const UAbilitySystemComponent* ASC,
		const FGameplayTag AbilityTag)
	{
		if (!ASC)
		{
			return nullptr;
		}
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (Spec.Ability && Spec.Ability->GetAssetTags().HasTagExact(AbilityTag))
			{
				return &Spec;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipChargeDamageMathTest,
	"ArtisticSW.Enemy.Ship.Ability.ChargeApproachDamageMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipChargeDamageMathTest::RunTest(const FString& Parameters)
{
	TestEqual(
		TEXT("Only center-line closing velocity contributes"),
		FEnemyShipSkillMath::CalculateApproachSpeed(
			FVector::ZeroVector,
			FVector(1000.0f, 500.0f, 0.0f),
			FVector(2000.0f, 0.0f, 0.0f),
			FVector(200.0f, 500.0f, 0.0f)),
		800.0f);
	TestEqual(
		TEXT("Separating ships have zero approach speed"),
		FEnemyShipSkillMath::CalculateApproachSpeed(
			FVector::ZeroVector,
			FVector(-100.0f, 0.0f, 0.0f),
			FVector(1000.0f, 0.0f, 0.0f),
			FVector::ZeroVector),
		0.0f);
	TestEqual(
		TEXT("Damage subtracts the threshold before applying its coefficient"),
		FEnemyShipSkillMath::CalculateChargeDamage(800.0f, 100.0f, 0.05f, 500.0f),
		35.0f);
	TestEqual(
		TEXT("Damage cap is enforced"),
		FEnemyShipSkillMath::CalculateChargeDamage(20000.0f, 100.0f, 0.05f, 500.0f),
		500.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipTorpedoBallisticsTest,
	"ArtisticSW.Enemy.Ship.Ability.TorpedoFixedSpeedTargetBallistics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipTorpedoBallisticsTest::RunTest(const FString& Parameters)
{
	const FVector Start(0.0f, 0.0f, 100.0f);
	const FVector Target(3000.0f, 0.0f, 100.0f);
	FVector Velocity;
	float FlightTime = 0.0f;
	float SolvedAngle = 0.0f;
	TestTrue(
		TEXT("A fixed-speed solution exists"),
		FEnemyShipSkillMath::SuggestBallisticVelocity(
			Start, Target, 3000.0f, 980.0f, 70.0f, Velocity, FlightTime, SolvedAngle));
	TestTrue(TEXT("Launch speed remains fixed"), FMath::IsNearlyEqual(Velocity.Size(), 3000.0f, 0.1f));
	TestTrue(TEXT("70-degree preference selects the high solution"), SolvedAngle > 45.0f);
	const FVector IntegratedEndpoint = Start + Velocity * FlightTime
		+ FVector(0.0f, 0.0f, -0.5f * 980.0f * FlightTime * FlightTime);
	TestTrue(TEXT("Solved arc reaches the fixed target"), IntegratedEndpoint.Equals(Target, 0.5f));

	TestFalse(
		TEXT("Unreachable fixed speed and target fail without NaN"),
		FEnemyShipSkillMath::SuggestBallisticVelocity(
			Start, FVector(20000.0f, 0.0f, 100.0f), 1000.0f, 980.0f, 70.0f,
			Velocity, FlightTime, SolvedAngle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipAbilityIntegrationTest,
	"ArtisticSW.Enemy.Ship.Ability.ChargeAndTorpedoIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipAbilityIntegrationTest::RunTest(const FString& Parameters)
{
	EnemyShipAbilityTests::FTestWorld TestWorld;
	AEnemyShip* EnemyShip = TestWorld.World->SpawnActor<AEnemyShip>(
		AEnemyShip::StaticClass(), FVector::ZeroVector, FRotator(0.0f, 90.0f, 0.0f));
	AShip* PlayerShip = TestWorld.World->SpawnActor<AShip>(
		AShip::StaticClass(), FVector(1000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	ACannon* Cannon = TestWorld.World->SpawnActor<ACannon>(
		ACannon::StaticClass(), FVector(0.0f, 300.0f, 100.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Enemy Ship spawned"), EnemyShip)
		|| !TestNotNull(TEXT("Player Ship spawned"), PlayerShip)
		|| !TestNotNull(TEXT("Enemy Cannon spawned"), Cannon))
	{
		return false;
	}

	EnemyShip->BuoyancyRoot->SetSimulatePhysics(true);
	PlayerShip->BuoyancyRoot->SetSimulatePhysics(true);
	Cannon->AttachToActor(EnemyShip, FAttachmentTransformRules::KeepWorldTransform);
	EnemyShip->RefreshMountedCannons();
	EnemyShip->GetNavigationComponent()->SetNavigationEnabled(true);
	EnemyShip->GetNavigationComponent()->SetTargetShip(PlayerShip);

	FShipStatSnapshot EnemyStats;
	EnemyStats.MaxHealth = 500.0f;
	EnemyStats.CannonDamage = 40.0f;
	EnemyStats.CannonFireCooldownSeconds = 30.0f;
	EnemyStats.CannonballSpeed = 3000.0f;
	EnemyStats.ForwardPropulsionMultiplier = 1.5f;
	EnemyStats.TurnTorqueMultiplier = 1.0f;
	EnemyShip->ApplyStatSnapshot(EnemyStats, true);
	FShipStatSnapshot PlayerStats;
	PlayerStats.MaxHealth = 500.0f;
	PlayerShip->ApplyStatSnapshot(PlayerStats, true);

	UAbilitySystemComponent* EnemyASC = EnemyShip->GetAbilitySystemComponent();
	UAbilitySystemComponent* PlayerASC = PlayerShip->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("Enemy ASC exists"), EnemyASC)
		|| !TestNotNull(TEXT("Player ASC exists"), PlayerASC))
	{
		return false;
	}
	EnemyASC->AddSpawnedAttribute(EnemyShip->GetShipAttributeSet());
	EnemyASC->InitAbilityActorInfo(EnemyShip, EnemyShip);
	PlayerASC->AddSpawnedAttribute(PlayerShip->GetShipAttributeSet());
	PlayerASC->InitAbilityActorInfo(PlayerShip, PlayerShip);
	const TArray<TSubclassOf<UGameplayAbility>> TestAbilities = {
		UGA_EnemyShipCharge::StaticClass(),
		UGA_EnemyShipLaunchTorpedo::StaticClass()
	};
	TestTrue(TEXT("Native Enemy Ship abilities are granted"), EnemyShip->GrantEnemyShipAbilityClasses(TestAbilities));
	EnemyShip->ApplyStatSnapshot(EnemyStats, true);
	PlayerShip->ApplyStatSnapshot(PlayerStats, true);

	TestNotNull(
		TEXT("Native AbilitySet entry grants Charge"),
		EnemyShipAbilityTests::FindAbilitySpec(EnemyASC, GameplayAbility_EnemyShip_Charge));
	TestNotNull(
		TEXT("Native AbilitySet entry grants Torpedo"),
		EnemyShipAbilityTests::FindAbilitySpec(EnemyASC, GameplayAbility_EnemyShip_LaunchTorpedo));

	FGameplayTagContainer ChargeTags(GameplayAbility_EnemyShip_Charge);
	TestTrue(TEXT("Charge activates by its GAS tag"), EnemyASC->TryActivateAbilitiesByTag(ChargeTags, false));
	EnemyShip->GetNavigationComponent()->TickComponent(0.016f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Charge aiming phase does not apply forward propulsion"), EnemyShip->GetCurrentAIPropulsionScale(), 1.0f);
	TestEqual(TEXT("Charge aiming phase applies its transient turn scale"), EnemyShip->GetCurrentAITurnScale(), 2.0f);
	TestFalse(TEXT("Charge aiming phase does not own the charging-state tag"), EnemyASC->HasMatchingGameplayTag(State_EnemyShip_Charging));
	const FGameplayAbilitySpec* ActiveChargeSpec = EnemyShipAbilityTests::FindAbilitySpec(
		EnemyASC, GameplayAbility_EnemyShip_Charge);
	TestTrue(TEXT("Charge ability remains active while aiming"), ActiveChargeSpec && ActiveChargeSpec->IsActive());

	EnemyASC->CancelAbilities(&ChargeTags);
	FGameplayTagContainer ChargeCooldownTags(Cooldown_EnemyShip_Charge);
	EnemyASC->RemoveActiveEffectsWithGrantedTags(ChargeCooldownTags);
	const FVector AlignedTargetLocation = EnemyShip->GetActorLocation()
		+ EnemyShip->GetActorForwardVector() * 1000.0f;
	PlayerShip->SetActorLocation(
		AlignedTargetLocation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	TestTrue(TEXT("Charge reactivates after the test removes its first cooldown"), EnemyASC->TryActivateAbilitiesByTag(ChargeTags, false));
	EnemyShip->GetNavigationComponent()->TickComponent(0.016f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Charge applies 2x transient propulsion scale"), EnemyShip->GetCurrentAIPropulsionScale(), 2.0f);
	TestEqual(TEXT("Charge keeps its transient turn scale while charging"), EnemyShip->GetCurrentAITurnScale(), 2.0f);
	TestTrue(TEXT("Charge owns its active-state tag"), EnemyASC->HasMatchingGameplayTag(State_EnemyShip_Charging));
	TestTrue(TEXT("Charge applies an independent GAS cooldown tag"), EnemyASC->HasMatchingGameplayTag(Cooldown_EnemyShip_Charge));

	const FVector ChargeVelocity = (PlayerShip->GetActorLocation() - EnemyShip->GetActorLocation())
		.GetSafeNormal2D() * 1000.0f;
	EnemyShip->BuoyancyRoot->SetPhysicsLinearVelocity(ChargeVelocity);
	PlayerShip->BuoyancyRoot->SetPhysicsLinearVelocity(FVector::ZeroVector);
	EnemyShip->BuoyancyRoot->ComponentVelocity = ChargeVelocity;
	PlayerShip->BuoyancyRoot->ComponentVelocity = FVector::ZeroVector;
	const float HealthBeforeCharge = PlayerASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute());
	FHitResult ChargeHit(PlayerShip, PlayerShip->BuoyancyRoot, PlayerShip->GetActorLocation(), FVector(-1.0f, 0.0f, 0.0f));
	EnemyShip->BuoyancyRoot->OnComponentHit.Broadcast(
		EnemyShip->BuoyancyRoot,
		PlayerShip,
		PlayerShip->BuoyancyRoot,
		FVector::ZeroVector,
		ChargeHit);
	const float HealthAfterCharge = PlayerASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute());
	TestTrue(TEXT("Charge damages only the designated Player Ship body"), HealthAfterCharge < HealthBeforeCharge);
	TestFalse(TEXT("Charge ends on valid Player Physics Root collision"), EnemyASC->HasMatchingGameplayTag(State_EnemyShip_Charging));
	EnemyShip->GetNavigationComponent()->TickComponent(0.016f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Charge releases Navigation Override on end"), EnemyShip->GetCurrentAIPropulsionScale(), 1.0f);
	TestEqual(TEXT("Charge releases transient turn scale on end"), EnemyShip->GetCurrentAITurnScale(), 1.0f);
	TestFalse(TEXT("Charge cannot reactivate during its own cooldown"), EnemyASC->TryActivateAbilitiesByTag(ChargeTags, false));

	FGameplayTagContainer TorpedoTags(GameplayAbility_EnemyShip_LaunchTorpedo);
	TestTrue(TEXT("Torpedo cooldown is independent from Charge cooldown"), EnemyASC->TryActivateAbilitiesByTag(TorpedoTags, false));
	AEnemyShipTorpedo* SpawnedTorpedo = nullptr;
	for (TActorIterator<AEnemyShipTorpedo> It(TestWorld.World); It; ++It)
	{
		SpawnedTorpedo = *It;
		break;
	}
	if (TestNotNull(TEXT("Torpedo GA spawns a dedicated Torpedo Actor"), SpawnedTorpedo))
	{
		TestEqual(TEXT("Torpedo snapshots CannonDamage x multiplier at launch"), SpawnedTorpedo->GetSnapshotDamage(), 60.0f);
		TestTrue(TEXT("Torpedo is locked to the selected Player Ship"), SpawnedTorpedo->GetDesignatedTarget() == PlayerShip);
		EnemyStats.CannonDamage = 100.0f;
		EnemyShip->ApplyStatSnapshot(EnemyStats, false);
		TestEqual(TEXT("Later attack-stat changes do not alter snapshot damage"), SpawnedTorpedo->GetSnapshotDamage(), 60.0f);

		const float HealthBeforeTorpedo = PlayerASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute());
		FHitResult TorpedoHit(PlayerShip, PlayerShip->ShipDamageMesh, PlayerShip->GetActorLocation(), FVector(-1.0f, 0.0f, 0.0f));
		if (UPrimitiveComponent* TorpedoCollision = Cast<UPrimitiveComponent>(SpawnedTorpedo->GetRootComponent()))
		{
			TorpedoCollision->OnComponentHit.Broadcast(
				TorpedoCollision,
				PlayerShip,
				PlayerShip->ShipDamageMesh,
				FVector::ZeroVector,
				TorpedoHit);
		}
		TestEqual(
			TEXT("Torpedo direct hit applies exactly its launch-time snapshot"),
			PlayerASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()),
			HealthBeforeTorpedo - 60.0f);
		TestTrue(TEXT("Torpedo destroys itself after one Player Ship hit"), SpawnedTorpedo->IsActorBeingDestroyed());
	}
	TestTrue(TEXT("Torpedo applies its own GAS cooldown"), EnemyASC->HasMatchingGameplayTag(Cooldown_EnemyShip_LaunchTorpedo));

	AddExpectedError(TEXT("projectile class is null"), EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("Torpedo launch does not consume normal cannon cooldown"), Cannon->FireCannon());
	return true;
}

#endif
