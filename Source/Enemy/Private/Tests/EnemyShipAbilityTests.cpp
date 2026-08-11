#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "Buoyancy/SWBuoyancyComponent.h"
#include "Cannon.h"
#include "Cannonball.h"
#include "CollisionChannels.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Ship.h"
#include "ShipAttributeSet.h"
#include "ShipAI/Abilities/EnemyShipSkillMath.h"
#include "ShipAI/Abilities/EnemyShipTorpedo.h"
#include "ShipAI/Abilities/EnemyShipObstacle.h"
#include "ShipAI/Abilities/EnemyShipObstacleProjectile.h"
#include "ShipAI/Abilities/GA_EnemyShipCharge.h"
#include "ShipAI/Abilities/GA_EnemyShipDeployObstacle.h"
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
	FEnemyShipTorpedoStraightTargetTest,
	"ArtisticSW.Enemy.Ship.Ability.TorpedoStraightLineTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipTorpedoStraightTargetTest::RunTest(const FString& Parameters)
{
	const FVector EnemyLocation(0.0f, 0.0f, 300.0f);
	const FVector PlayerLocation(3000.0f, 4000.0f, 125.0f);
	const FVector TargetPoint = UGA_EnemyShipLaunchTorpedo::CalculateLineTargetPoint(
		EnemyLocation,
		PlayerLocation,
		0.3f);
	TestTrue(
		TEXT("Alpha 0.3 internally divides the Enemy-to-Player segment"),
		TargetPoint.Equals(FVector(900.0f, 1200.0f, 247.5f), 0.01f));
	TestTrue(
		TEXT("Alpha 0 resolves to the Enemy location"),
		UGA_EnemyShipLaunchTorpedo::CalculateLineTargetPoint(
			EnemyLocation,
			PlayerLocation,
			0.0f).Equals(EnemyLocation));
	TestTrue(
		TEXT("Alpha 1 resolves to the Player location"),
		UGA_EnemyShipLaunchTorpedo::CalculateLineTargetPoint(
			EnemyLocation,
			PlayerLocation,
			1.0f).Equals(PlayerLocation));
	TestTrue(
		TEXT("Alpha is clamped above one"),
		UGA_EnemyShipLaunchTorpedo::CalculateLineTargetPoint(
			EnemyLocation,
			PlayerLocation,
			2.0f).Equals(PlayerLocation));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipObstacleTrajectoryTest,
	"ArtisticSW.Enemy.Ship.Ability.ObstacleTrajectoryAndTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipObstacleTrajectoryTest::RunTest(const FString& Parameters)
{
	const FVector EnemyLocation(0.0f, 0.0f, 250.0f);
	const FVector PlayerLocation(4000.0f, 2000.0f, 125.0f);
	const FVector TargetPoint = UGA_EnemyShipDeployObstacle::CalculateTargetPoint(
		EnemyLocation,
		PlayerLocation,
		0.25f,
		700.0f);
	TestTrue(
		TEXT("Obstacle target uses internal-division XY and independent absolute Z"),
		TargetPoint.Equals(FVector(1000.0f, 500.0f, 700.0f), 0.01f));

	const FVector Start(100.0f, -200.0f, 300.0f);
	FVector LaunchVelocity = FVector::ZeroVector;
	float TravelSeconds = 0.0f;
	if (TestTrue(
		TEXT("A reachable low ballistic arc resolves"),
		UGA_EnemyShipDeployObstacle::CalculateBallisticLaunchVelocity(
			Start,
			TargetPoint,
			3000.0f,
			-980.0f,
			false,
			LaunchVelocity,
			TravelSeconds)))
	{
		const FVector ReconstructedTarget = Start
			+ LaunchVelocity * TravelSeconds
			+ FVector(0.0f, 0.0f, -490.0f * FMath::Square(TravelSeconds));
		TestTrue(
			TEXT("Ballistic velocity reaches the authored conversion point"),
			ReconstructedTarget.Equals(TargetPoint, 0.1f));
		TestTrue(TEXT("The carrier has a non-zero curved flight duration"), TravelSeconds > 0.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipObstacleActorContractTest,
	"ArtisticSW.Enemy.Ship.Ability.ObstacleActorContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipObstacleActorContractTest::RunTest(const FString& Parameters)
{
	EnemyShipAbilityTests::FTestWorld TestWorld;
	AEnemyShipObstacle* Obstacle = TestWorld.World->SpawnActor<AEnemyShipObstacle>();
	AEnemyShipObstacleProjectile* Projectile = TestWorld.World->SpawnActor<AEnemyShipObstacleProjectile>();
	if (!TestNotNull(TEXT("Obstacle actor spawns"), Obstacle)
		|| !TestNotNull(TEXT("Obstacle carrier spawns"), Projectile))
	{
		return false;
	}

	USphereComponent* ObstacleCollision = Obstacle->FindComponentByClass<USphereComponent>();
	UBoxComponent* ObstacleBlocker = Obstacle->FindComponentByClass<UBoxComponent>();
	USWBuoyancyComponent* ObstacleBuoyancy = Obstacle->FindComponentByClass<USWBuoyancyComponent>();
	UProjectileMovementComponent* CarrierMovement = Projectile->FindComponentByClass<UProjectileMovementComponent>();
	TestNotNull(TEXT("Obstacle owns a physics collision root"), ObstacleCollision);
	TestNotNull(TEXT("Obstacle owns SW buoyancy"), ObstacleBuoyancy);
	TestNotNull(TEXT("Carrier owns projectile movement"), CarrierMovement);
	if (ObstacleCollision)
	{
		TestEqual(TEXT("Obstacle uses its dedicated object channel"), ObstacleCollision->GetCollisionObjectType(), ECC_GameTraceChannel6);
		TestEqual(TEXT("Obstacle blocks Player cannonballs"), ObstacleCollision->GetCollisionResponseToChannel(ECC_GameTraceChannel2), ECR_Block);
		TestEqual(TEXT("Obstacle ignores Enemy cannonballs"), ObstacleCollision->GetCollisionResponseToChannel(ECC_GameTraceChannel3), ECR_Ignore);
		TestTrue(TEXT("Obstacle locks horizontal translation"), ObstacleCollision->BodyInstance.bLockXTranslation && ObstacleCollision->BodyInstance.bLockYTranslation);
		TestFalse(TEXT("Obstacle keeps vertical translation free for buoyancy"), ObstacleCollision->BodyInstance.bLockZTranslation);
	}
	if (TestNotNull(TEXT("Obstacle owns a separate kinematic blocker"), ObstacleBlocker))
	{
		TestTrue(TEXT("Obstacle blocks across the authored visual footprint"), ObstacleBlocker->GetUnscaledBoxExtent().Equals(FVector(512.0f)));
		TestFalse(TEXT("Obstacle blocker is kinematic"), ObstacleBlocker->IsSimulatingPhysics());
		TestEqual(TEXT("Obstacle blocker uses its dedicated object channel"), ObstacleBlocker->GetCollisionObjectType(), ECC_GameTraceChannel6);
	}
	if (ObstacleBuoyancy)
	{
		TestEqual(TEXT("Obstacle owns one buoyancy pontoon"), ObstacleBuoyancy->GetPontoons().Num(), 1);
		if (!ObstacleBuoyancy->GetPontoons().IsEmpty())
		{
			TestEqual(TEXT("Obstacle uses torpedo-matched pontoon radius"), ObstacleBuoyancy->GetPontoons()[0].Radius, 50.0f);
		}
		TestEqual(TEXT("Obstacle uses torpedo-matched deep recovery"), ObstacleBuoyancy->GetForceSettings().DeepWaterBuoyancyMultiplier, 3.0f);
	}
	if (CarrierMovement)
	{
		TestFalse(TEXT("Carrier sweep collision is disabled"), CarrierMovement->bSweepCollision);
		TestEqual(TEXT("Carrier preserves the normal cannonball gravity scale"), CarrierMovement->ProjectileGravityScale, 1.0f);
	}

	FCollisionResponseTemplate WaterBodyProfile;
	if (TestTrue(
		TEXT("WaterBodyCollision profile exists"),
		UCollisionProfile::Get()->GetProfileTemplate(TEXT("WaterBodyCollision"), WaterBodyProfile)))
	{
		TestEqual(
			TEXT("WaterBody reciprocates the obstacle overlap so buoyancy can activate"),
			WaterBodyProfile.ResponseToChannels.GetResponse(ECC_EnemyShipObstacle),
			ECR_Overlap);
	}

	const UGameplayAbility* AbilityCDO = UGA_EnemyShipDeployObstacle::StaticClass()->GetDefaultObject<UGameplayAbility>();
	TestTrue(
		TEXT("Obstacle ability exposes the tag consumed by SkillModule/Pattern/BT selection"),
		AbilityCDO && AbilityCDO->GetAssetTags().HasTagExact(GameplayAbility_EnemyShip_DeployObstacle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipObstacleCannonSweepTest,
	"ArtisticSW.Enemy.Ship.Ability.ObstacleCannonSweepByTeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipObstacleCannonSweepTest::RunTest(const FString& Parameters)
{
	EnemyShipAbilityTests::FTestWorld TestWorld;
	AEnemyShipObstacle* Obstacle = TestWorld.World->SpawnActor<AEnemyShipObstacle>(
		AEnemyShipObstacle::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	AShip* PlayerShip = TestWorld.World->SpawnActor<AShip>(
		AShip::StaticClass(), FVector(0.0f, 5000.0f, 0.0f), FRotator::ZeroRotator);
	AShip* EnemyShip = TestWorld.World->SpawnActor<AShip>(
		AShip::StaticClass(), FVector(0.0f, -5000.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Obstacle spawned"), Obstacle)
		|| !TestNotNull(TEXT("Player launching ship spawned"), PlayerShip)
		|| !TestNotNull(TEXT("Enemy launching ship spawned"), EnemyShip))
	{
		return false;
	}

	EnemyShip->Tags.Remove(TEXT("Player"));
	EnemyShip->Tags.AddUnique(TEXT("Enemy"));
	USphereComponent* ObstacleCollision = Obstacle->FindComponentByClass<USphereComponent>();
	UBoxComponent* ObstacleBlocker = Obstacle->FindComponentByClass<UBoxComponent>();
	if (!TestNotNull(TEXT("Obstacle buoyancy root exists"), ObstacleCollision)
		|| !TestNotNull(TEXT("Obstacle blocking box exists"), ObstacleBlocker))
	{
		return false;
	}
	ObstacleCollision->SetSimulatePhysics(false);
	ObstacleCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ObstacleBlocker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	ACannonball* PlayerCannonball = TestWorld.World->SpawnActor<ACannonball>(
		ACannonball::StaticClass(), FVector(-1000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Player cannonball spawned"), PlayerCannonball))
	{
		return false;
	}
	PlayerCannonball->InitializeProjectile(PlayerShip, 10.0f, 3000.0f);
	USphereComponent* PlayerSphere = PlayerCannonball->FindComponentByClass<USphereComponent>();
	FHitResult PlayerHit;
	if (TestNotNull(TEXT("Player cannonball collision exists"), PlayerSphere))
	{
		PlayerSphere->MoveComponent(
			FVector(2000.0f, 0.0f, 0.0f),
			FRotator::ZeroRotator,
			true,
			&PlayerHit,
			MOVECOMP_NoFlags,
			ETeleportType::None);
		TestTrue(TEXT("Player cannonball sweep blocks on obstacle"), PlayerHit.bBlockingHit);
		TestEqual(TEXT("Player cannonball sweep hits obstacle actor"), PlayerHit.GetActor(), static_cast<AActor*>(Obstacle));
	}

	ACannonball* EnemyCannonball = TestWorld.World->SpawnActor<ACannonball>(
		ACannonball::StaticClass(), FVector(-1000.0f, 0.0f, 100.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Enemy cannonball spawned"), EnemyCannonball))
	{
		return false;
	}
	EnemyCannonball->InitializeProjectile(EnemyShip, 10.0f, 3000.0f);
	USphereComponent* EnemySphere = EnemyCannonball->FindComponentByClass<USphereComponent>();
	FHitResult EnemyHit;
	if (TestNotNull(TEXT("Enemy cannonball collision exists"), EnemySphere))
	{
		EnemySphere->MoveComponent(
			FVector(2000.0f, 0.0f, 0.0f),
			FRotator::ZeroRotator,
			true,
			&EnemyHit,
			MOVECOMP_NoFlags,
			ETeleportType::None);
		TestFalse(TEXT("Enemy cannonball sweep passes through obstacle"), EnemyHit.bBlockingHit);
	}
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
	const FGameplayAbilitySpec* ActiveTorpedoSpec = EnemyShipAbilityTests::FindAbilitySpec(
		EnemyASC, GameplayAbility_EnemyShip_LaunchTorpedo);
	TestTrue(TEXT("Torpedo volley remains active between scheduled launches"), ActiveTorpedoSpec && ActiveTorpedoSpec->IsActive());
	AEnemyShipTorpedo* SpawnedTorpedo = nullptr;
	for (TActorIterator<AEnemyShipTorpedo> It(TestWorld.World); It; ++It)
	{
		SpawnedTorpedo = *It;
		break;
	}
	if (TestNotNull(TEXT("Torpedo GA spawns a dedicated Torpedo Actor"), SpawnedTorpedo))
	{
		USWBuoyancyComponent* TorpedoBuoyancy = SpawnedTorpedo->FindComponentByClass<USWBuoyancyComponent>();
		UProjectileMovementComponent* TorpedoMovement =
			SpawnedTorpedo->FindComponentByClass<UProjectileMovementComponent>();
		TestNotNull(
			TEXT("Torpedo owns the same server-authority SW buoyancy component pattern as floating chests"),
			TorpedoBuoyancy);
		TestTrue(
			TEXT("Torpedo keeps buoyancy disabled during projectile flight"),
			TorpedoBuoyancy && !TorpedoBuoyancy->IsActive());
		TestEqual(
			TEXT("Torpedo flight is straight before water entry"),
			TorpedoMovement ? TorpedoMovement->ProjectileGravityScale : -1.0f,
			0.0f);
		TestTrue(TEXT("Torpedo receives a configurable total lifetime"), SpawnedTorpedo->GetLifeSpan() > 0.0f);
		TestEqual(TEXT("Torpedo snapshots CannonDamage x multiplier at launch"), SpawnedTorpedo->GetSnapshotDamage(), 60.0f);
		TestTrue(TEXT("Torpedo is locked to the selected Player Ship"), SpawnedTorpedo->GetDesignatedTarget() == PlayerShip);
		EnemyStats.CannonDamage = 100.0f;
		EnemyShip->ApplyStatSnapshot(EnemyStats, false);
		TestEqual(TEXT("Later attack-stat changes do not alter snapshot damage"), SpawnedTorpedo->GetSnapshotDamage(), 60.0f);

		const float HealthBeforeTorpedo = PlayerASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute());
		TestFalse(
			TEXT("Player ShipDamageMesh does not require overlap generation"),
			PlayerShip->ShipDamageMesh->GetGenerateOverlapEvents());
		TestEqual(
			TEXT("Player ShipDamageMesh blocks the EnemyCannon object channel"),
			PlayerShip->ShipDamageMesh->GetCollisionResponseToChannel(ECC_GameTraceChannel3),
			ECR_Block);
		if (UPrimitiveComponent* TorpedoCollision = Cast<UPrimitiveComponent>(SpawnedTorpedo->GetRootComponent()))
		{
			UStaticMesh* DamageTestMesh = LoadObject<UStaticMesh>(
				nullptr,
				TEXT("/Engine/BasicShapes/Cube.Cube"));
			TestNotNull(TEXT("Damage Mesh test shape loads"), DamageTestMesh);
			PlayerShip->ShipDamageMesh->SetStaticMesh(DamageTestMesh);
			PlayerShip->ShipDamageMesh->SetWorldScale3D(FVector(2.0f));
			PlayerShip->ShipDamageMesh->SetCollisionProfileName(TEXT("PlayerShipDamage"));
			PlayerShip->ShipDamageMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

			UStaticMeshComponent* WaterTestComponent = NewObject<UStaticMeshComponent>(
				PlayerShip,
				TEXT("WaterTestComponent"));
			WaterTestComponent->RegisterComponent();
			FHitResult WaterEntryHit;
			TorpedoCollision->OnComponentBeginOverlap.Broadcast(
				TorpedoCollision,
				PlayerShip,
				WaterTestComponent,
				0,
				true,
				WaterEntryHit);
			TestTrue(
				TEXT("Torpedo enters its physics/buoyancy water phase"),
				SpawnedTorpedo->HasEnteredWaterForDiagnostics());

			SpawnedTorpedo->SetActorLocation(
				PlayerShip->ShipDamageMesh->GetComponentLocation(),
				false,
				nullptr,
				ETeleportType::TeleportPhysics);
			SpawnedTorpedo->Tick(0.016f);
		}
		TestEqual(
			TEXT("Floating Torpedo sweeps the query-only ShipDamageMesh and applies its snapshot"),
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
