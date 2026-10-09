#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "Combat/PlayerBowShotPreparation.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GASCombatLibrary.h"
#include "Item/Projectiles/PlayerArrowProjectile.h"
#include "Settings_Item.h"
#include "Ship.h"
#include "UObject/UnrealType.h"

namespace PlayerBowLaunchTests
{
	struct FWorldScope
	{
		TGuardValue<TSoftObjectPtr<UDataTable>> CraftingGuard{GetMutableDefault<USettings_Item>()->CraftingRecipeDataTable, {}};
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldScope() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
		~FWorldScope() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }

		AShip* MakeShip()
		{
			AShip* Ship = World->SpawnActor<AShip>();
			if (!Ship) return nullptr;
			Ship->BuoyancyRoot->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Ship->BuoyancyRoot->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Ship->BuoyancyRoot->SetSimulatePhysics(true);
			Ship->SetActorLocation(FVector(10000, 10000, 1000));
			return Ship;
		}
	};

	FProjectileShotInput MakeInput(const FVector& Muzzle)
	{
		FProjectileShotInput Input;
		Input.ShotId = FGuid::NewGuid();
		Input.MuzzleTransform = FTransform(FRotator(17, 80, -23), Muzzle);
		Input.AimPoint = Muzzle + FVector(10000, 0, 0);
		Input.Speed = 4000.0;
		Input.GravityZ = -196.0;
		return Input;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerBowShipInheritanceTest,
	"ArtisticSW.PlayerBow.ShipPointVelocity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerBowShipInheritanceTest::RunTest(const FString& Parameters)
{
	PlayerBowLaunchTests::FWorldScope Scope;
	AShip* Ship = Scope.MakeShip();
	ACharacter* Shooter = Scope.World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("Ship"), Ship) || !TestNotNull(TEXT("Shooter"), Shooter)) return false;
	Shooter->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Shooter->SetBase(Ship->GetDeckMeshComplex());
	Shooter->GetCharacterMovement()->Velocity = FVector(600, -200, 0);
	const FVector LinearVelocity(1200, -300, 80);
	const FVector AngularVelocity(0.15, -0.25, 0.4);
	Ship->BuoyancyRoot->SetPhysicsLinearVelocity(LinearVelocity);
	Ship->BuoyancyRoot->SetPhysicsAngularVelocityInRadians(AngularVelocity);
	if (!TestTrue(TEXT("Ship uses a real physics body"), Ship->BuoyancyRoot->IsSimulatingPhysics())) return false;
	TestTrue(TEXT("Physics linear velocity is configured"), Ship->BuoyancyRoot->GetPhysicsLinearVelocity().Equals(LinearVelocity, 0.1));
	TestTrue(TEXT("Physics angular velocity is configured"), Ship->BuoyancyRoot->GetPhysicsAngularVelocityInRadians().Equals(AngularVelocity, 0.001));

	for (const FVector Offset : { FVector(400, 100, 600), FVector(-400, -100, 600) })
	{
		const FVector Muzzle = Ship->BuoyancyRoot->GetCenterOfMass() + Offset;
		const FProjectileShotInput Input = PlayerBowLaunchTests::MakeInput(Muzzle);
		FProjectileShotSnapshot Shot;
		if (!TestTrue(TEXT("Grounded shot prepares"), PlayerBowShotPreparation::Prepare(Shooter, Input, Shot))) return false;
		const FVector PointVelocity = LinearVelocity + FVector::CrossProduct(AngularVelocity, Offset);
		TestTrue(TEXT("Socket velocity includes roll, pitch and yaw at the actual muzzle"), Shot.InheritedVelocity.Equals(PointVelocity, 0.1));
		TestTrue(TEXT("Velocity sums once and excludes character walking"), Shot.WorldVelocity.Equals(FVector(4000, 0, 0) + PointVelocity, 0.1));
		TestTrue(TEXT("World speed can exceed the authored relative speed"), Shot.WorldVelocity.Size() > Input.Speed);
		TestTrue(TEXT("Spawn rotation follows combined world velocity"), Shot.SpawnTransform.GetRotation().GetForwardVector().Equals(Shot.WorldVelocity.GetSafeNormal(), 0.001));
		TestTrue(TEXT("Relative aim remains separate from inherited velocity"), Shot.Input.AimDirection.Equals(FVector::ForwardVector));
		TestTrue(TEXT("Gravity remains the authored natural drop"), Shot.Input.GravityZ == Input.GravityZ);
	}

	const auto Input = PlayerBowLaunchTests::MakeInput(Ship->GetActorLocation() + FVector(0, 0, 600));
	FProjectileShotSnapshot Shot;
	Ship->BuoyancyRoot->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Ship->BuoyancyRoot->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	TestTrue(TEXT("Stationary ship keeps direct relative aim"), PlayerBowShotPreparation::Prepare(Shooter, Input, Shot)
		&& Shot.InheritedVelocity.IsNearlyZero() && Shot.WorldVelocity.Equals(FVector(4000, 0, 0)));

	Shooter->SetOwner(Ship); // Lifecycle ownership is not physical support.
	Shooter->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	Shooter->SetBase(nullptr);
	Ship->BuoyancyRoot->SetPhysicsLinearVelocity(LinearVelocity);
	TestTrue(TEXT("Air shot ignores a stale ship owner and character velocity"), PlayerBowShotPreparation::Prepare(Shooter, Input, Shot)
		&& !Shot.bHasMotionSample && Shot.InheritedVelocity.IsNearlyZero() && Shot.WorldVelocity.Equals(FVector(4000, 0, 0)));

	Shooter->GetCharacterMovement()->SetMovementMode(MOVE_None);
	Shooter->AttachToComponent(Ship->GetDeckMeshComplex(), FAttachmentTransformRules::KeepWorldTransform);
	TestTrue(TEXT("Attached passenger inherits ship motion without stale walking velocity"), PlayerBowShotPreparation::Prepare(Shooter, Input, Shot)
		&& Shot.InheritedVelocity.Equals(LinearVelocity, 0.1));
	Ship->BuoyancyRoot->SetSimulatePhysics(false);
	TestFalse(TEXT("Unavailable supported ship velocity rejects the shot"), PlayerBowShotPreparation::Prepare(Shooter, Input, Shot));
	TestTrue(TEXT("Rejected preparation leaves no partially committed velocity"), Shot.WorldVelocity.IsNearlyZero());
	Shooter->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	TestTrue(TEXT("Unattached ground shot still prepares without a ship sample"), PlayerBowShotPreparation::Prepare(Shooter, Input, Shot)
		&& Shot.InheritedVelocity.IsNearlyZero() && Shot.WorldVelocity.Equals(FVector(4000, 0, 0)));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerBowIndependentFlightTest,
	"ArtisticSW.PlayerBow.IndependentFlight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerBowIndependentFlightTest::RunTest(const FString& Parameters)
{
	PlayerBowLaunchTests::FWorldScope Scope;
	APlayerArrowProjectile* Defaults = GetMutableDefault<APlayerArrowProjectile>();
	FFloatProperty* SpeedProperty = FindFProperty<FFloatProperty>(AArrowProjectile::StaticClass(), TEXT("InitialLaunchSpeed"));
	if (!TestNotNull(TEXT("Arrow launch speed is exposed as an editor property"), SpeedProperty)) return false;
	float& AuthoredSpeed = *SpeedProperty->ContainerPtrToValuePtr<float>(Defaults);
	TGuardValue<float> SpeedGuard(AuthoredSpeed, 0.0f);
	TestEqual(TEXT("Zero preserves the bow's existing draw speed"), Defaults->ResolveInitialLaunchSpeed(4000.0f), 4000.0f);
	AuthoredSpeed = 6200.0f;
	AShip* Ship = Scope.MakeShip();
	ACharacter* Shooter = Scope.World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("Ship"), Ship) || !TestNotNull(TEXT("Shooter"), Shooter)) return false;
	Shooter->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Shooter->SetBase(Ship->GetDeckMeshComplex());
	Ship->BuoyancyRoot->SetPhysicsLinearVelocity(FVector(2000, 500, 100));
	auto Input = PlayerBowLaunchTests::MakeInput(Ship->GetActorLocation() + FVector(0, 0, 600));
	Input.Speed = Defaults->ResolveInitialLaunchSpeed(Input.Speed);
	FProjectileShotSnapshot Shot;
	if (!TestTrue(TEXT("Ship shot prepares"), PlayerBowShotPreparation::Prepare(Shooter, Input, Shot))) return false;
	TestTrue(TEXT("Authored launch speed reaches the snapshot before ship inertia is added"),
		FMath::IsNearlyEqual(Shot.Input.Speed, 6200.0)
		&& (Shot.WorldVelocity - Shot.InheritedVelocity).Equals(Shot.Input.AimDirection * 6200.0, 0.01));

	UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Shooter);
	Shooter->AddInstanceComponent(ASC);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Shooter, Shooter);
	ASC->AddAttributeSetSubobject(NewObject<UBaseAttributeSet>(Shooter));
	ASC->SetNumericAttributeBase(UBaseAttributeSet::GetStrengthAttribute(), 30.0f);

	// Change the ship after commit. Neither construction nor flight may sample it again.
	Ship->BuoyancyRoot->SetPhysicsLinearVelocity(FVector(-3000, -1000, 0));
	Ship->BuoyancyRoot->SetPhysicsAngularVelocityInRadians(FVector(0, 0, 1));
	for (const TCHAR* Path : { TEXT("/Script/ArtisticSWCore.PlayerArrowProjectile"),
		TEXT("/Game/GameplayAbilitySystem/Weapon/BP_Arrow.BP_Arrow_C") })
	{
		UClass* Class = LoadClass<APlayerArrowProjectile>(nullptr, Path);
		if (!TestNotNull(Path, Class)) continue;
		auto* Arrow = Scope.World->SpawnActorDeferred<APlayerArrowProjectile>(Class, Shot.SpawnTransform, Shooter, Shooter,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!TestNotNull(TEXT("Arrow spawns"), Arrow)) continue;
		Arrow->FinishSpawning(Shot.SpawnTransform);
		Arrow->AttachToComponent(Ship->GetDeckMeshComplex(), FAttachmentTransformRules::KeepWorldTransform);
		auto* Movement = Arrow->GetProjectileMovement();
		Movement->InitialSpeed = 100.0f;
		Movement->MaxSpeed = 2000.0f;
		Movement->bInitialVelocityInLocalSpace = true;
		FStrengthDamageRequest Damage;
		Damage.SourceASC = ASC;
		Damage.InstigatorActor = Shooter;
		Damage.EffectCauser = Arrow;
		Damage.AttackCoefficient = 1.0f;
		TestTrue(TEXT("Strength payload initializes"), Arrow->InitializeStrengthDamage(ASC, Shooter, UGASCombatLibrary::MakeStrengthDamageEffectSpec(Damage)));
		if (!TestTrue(TEXT("Player shot launches"), Arrow->LaunchPlayerShot(Shot, nullptr))) { Arrow->Destroy(); continue; }
		TestNull(TEXT("Launched arrow has no ship parent"), Arrow->GetAttachParentActor());
		TestTrue(TEXT("Construction uses the committed world origin and velocity"),
			Arrow->GetActorLocation().Equals(Shot.SpawnTransform.GetLocation(), 0.01) && Movement->Velocity.Equals(Shot.WorldVelocity, 0.01));
		TestTrue(TEXT("Relative fire speed does not clamp or reinterpret world speed"),
			Movement->MaxSpeed == 0.0f && Movement->InitialSpeed == 0.0f && !Movement->bInitialVelocityInLocalSpace);

		const FVector Start = Arrow->GetActorLocation();
		Movement->TickComponent(0.02f, LEVELTICK_All, nullptr);
		TestTrue(TEXT("Release frame does not integrate pre-launch time"), Arrow->GetActorLocation().Equals(Start));
		Ship->SetActorTransform(FTransform(FRotator(20, 90, 15), FVector(-5000, -5000, 0)), false, nullptr, ETeleportType::TeleportPhysics);
		TestTrue(TEXT("Ship movement cannot drag the arrow"), Arrow->GetActorLocation().Equals(Start));
		{
			TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter + 1);
			Movement->TickComponent(0.02f, LEVELTICK_All, nullptr);
		}
		const FVector ExpectedLocation = Start + Shot.WorldVelocity * 0.02 + FVector(0, 0, 0.5 * Input.GravityZ * 0.02 * 0.02);
		TestTrue(TEXT("Independent flight integrates the captured inertia and natural gravity"), Arrow->GetActorLocation().Equals(ExpectedLocation, 0.2));
		TestTrue(TEXT("Flight velocity changes only by gravity"), Movement->Velocity.Equals(Shot.WorldVelocity + FVector(0, 0, Input.GravityZ * 0.02), 0.1));
		Arrow->SetRole(ROLE_SimulatedProxy);
		TestFalse(TEXT("A client cannot relaunch or add ship velocity again"), Arrow->LaunchPlayerShot(Shot, nullptr));
		Arrow->SetRole(ROLE_Authority);
		Arrow->Destroy();
	}

	Ship->BuoyancyRoot->SetPhysicsLinearVelocity(FVector(-Input.Speed, 0, 0));
	Ship->BuoyancyRoot->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	if (!TestTrue(TEXT("Exactly cancelling velocities still prepare a physical arrow"),
		PlayerBowShotPreparation::Prepare(Shooter, Input, Shot) && Shot.WorldVelocity.IsNearlyZero())) return false;
	auto* FallingArrow = Scope.World->SpawnActor<APlayerArrowProjectile>();
	if (!TestNotNull(TEXT("Zero-world-speed arrow"), FallingArrow)) return false;
	FStrengthDamageRequest Damage;
	Damage.SourceASC = ASC;
	Damage.InstigatorActor = Shooter;
	Damage.EffectCauser = FallingArrow;
	Damage.AttackCoefficient = 1.0f;
	FallingArrow->InitializeStrengthDamage(ASC, Shooter, UGASCombatLibrary::MakeStrengthDamageEffectSpec(Damage));
	TestTrue(TEXT("Zero-world-speed arrow launches"), FallingArrow->LaunchPlayerShot(Shot, nullptr));
	{
		TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter + 1);
		FallingArrow->GetProjectileMovement()->TickComponent(0.02f, LEVELTICK_All, nullptr);
	}
	TestTrue(TEXT("Exactly cancelled initial motion falls naturally under gravity"),
		FallingArrow->GetActorLocation().Z < Input.MuzzleTransform.GetLocation().Z
		&& FallingArrow->GetProjectileMovement()->Velocity.Equals(FVector(0, 0, Input.GravityZ * 0.02), 0.1));
	FallingArrow->Destroy();
	return !HasAnyErrors();
}

#endif
