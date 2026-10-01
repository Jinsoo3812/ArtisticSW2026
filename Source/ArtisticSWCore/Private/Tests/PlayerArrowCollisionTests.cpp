#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "CollisionChannels.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Item/Projectiles/ArrowCollisionQuery.h"
#include "Item/Projectiles/PlayerArrowProjectile.h"
#include "Item/Projectiles/ProjectileShotPreparation.h"

namespace PlayerArrowCollisionTests
{
	struct FWorldScope
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldScope() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
		~FWorldScope() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	};

	UBoxComponent* AddBlockingBox(AActor* Actor, const FVector& Location)
	{
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->AddInstanceComponent(Box);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(30.0));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ECC_WorldDynamic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Box;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerArrowIgnoresShooterTest,
	"ArtisticSW.Item.Arrow.PlayerIgnoresShooter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerArrowIgnoresShooterTest::RunTest(const FString& Parameters)
{
	// World initialization loads the existing crafting table, which has unrelated invalid quest rows.
	AddExpectedError(TEXT("QuestItem (has an invalid ResultItemTag|contains an invalid ingredient)"),
		EAutomationExpectedErrorFlags::Contains, 0);
	PlayerArrowCollisionTests::FWorldScope Scope;
	APawn* Shooter = Scope.World->SpawnActor<APawn>();
	AActor* Wall = Scope.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Shooter exists"), Shooter) || !TestNotNull(TEXT("Wall exists"), Wall)) return false;
	PlayerArrowCollisionTests::AddBlockingBox(Shooter, FVector::ZeroVector);
	PlayerArrowCollisionTests::AddBlockingBox(Wall, FVector(200.0, 0.0, 0.0));

	FHitResult Hit;
	const FVector Start = FVector::ZeroVector;
	const FVector End(400.0, 0.0, 0.0);
	const FCollisionQueryParams Unfiltered(SCENE_QUERY_STAT(PlayerArrowSelfCollisionTest));
	TestTrue(TEXT("Without shooter exclusion the launch starts in a blocking body"),
		ArrowCollisionQuery::SweepObstacles(Scope.World, Start, End, FQuat::Identity,
			FVector(8.0, 1.0, 1.0), Unfiltered, Hit) && Hit.GetActor() == Shooter && Hit.bStartPenetrating);

	for (const TCHAR* Path : {
		TEXT("/Script/ArtisticSWCore.PlayerArrowProjectile"),
		TEXT("/Game/GameplayAbilitySystem/Weapon/BP_Arrow.BP_Arrow_C")})
	{
		UClass* Class = LoadClass<APlayerArrowProjectile>(nullptr, Path);
		if (!TestNotNull(Path, Class)) continue;
		const FTransform SpawnTransform(Start);
		APlayerArrowProjectile* Arrow = Scope.World->SpawnActorDeferred<APlayerArrowProjectile>(
			Class, SpawnTransform, Shooter, Shooter, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!TestNotNull(TEXT("Player arrow exists"), Arrow)) continue;

		// Exercise the real flight query before BeginPlay/GA can populate MovementIgnoredActors.
		TestTrue(TEXT("Deferred player arrow sweeps past its shooter to the wall"),
			ArrowCollisionQuery::SweepFlight(*Arrow, Start, End, FQuat::Identity, Hit)
			&& Hit.GetActor() == Wall && !Hit.bStartPenetrating);
		Arrow->FinishSpawning(SpawnTransform);
		TestTrue(TEXT("Constructed player arrow still ignores its shooter and blocks on the wall"),
			ArrowCollisionQuery::SweepFlight(*Arrow, Start, End, FQuat::Identity, Hit)
			&& Hit.GetActor() == Wall && !Hit.bStartPenetrating);

		FProjectileShotInput Input;
		Input.ShotId = FGuid::NewGuid();
		Input.MuzzleTransform = SpawnTransform;
		Input.AimPoint = FVector(10000.0, 0.0, 0.0);
		Input.AimDirection = FVector::ForwardVector;
		Input.Speed = 4000.0;
		Input.GravityZ = -196.0;
		FProjectileShotSnapshot Shot;
		TestTrue(TEXT("Player shot keeps level initial aim with gravity enabled"),
			ProjectileShotPreparation::Prepare(Shooter, Input, Shot)
			&& Shot.WorldVelocity.Equals(FVector(4000.0, 0.0, 0.0)) && Shot.Input.GravityZ == Input.GravityZ);
		Arrow->Destroy();
	}
	return !HasAnyErrors();
}

#endif
