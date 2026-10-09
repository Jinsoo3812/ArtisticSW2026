#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "CollisionChannels.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Item/Projectiles/ArrowCollisionQuery.h"
#include "Item/Projectiles/PlayerArrowProjectile.h"
#include "PCGVolume.h"
#include "PhysicsEngine/BodySetup.h"

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

		Arrow->Destroy();
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArrowIgnoresPCGBoundsTest,
	"ArtisticSW.Item.Arrow.IgnoresPCGBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FArrowIgnoresPCGBoundsTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("QuestItem (has an invalid ResultItemTag|contains an invalid ingredient)"),
		EAutomationExpectedErrorFlags::Contains, 0);
	PlayerArrowCollisionTests::FWorldScope Scope;
	APCGVolume* Volume = Scope.World->SpawnActor<APCGVolume>();
	AActor* ProfileBounds = Scope.World->SpawnActor<AActor>();
	AActor* Wall = Scope.World->SpawnActor<AActor>();
	AArrowProjectile* Arrow = Scope.World->SpawnActor<AArrowProjectile>();
	if (!TestNotNull(TEXT("PCG volume"), Volume) || !TestNotNull(TEXT("Profile bounds"), ProfileBounds)
		|| !TestNotNull(TEXT("Wall"), Wall) || !TestNotNull(TEXT("Arrow"), Arrow)) return false;

	UBrushComponent* Brush = Volume->GetBrushComponent();
	Brush->UnregisterComponent();
	Brush->BrushBodySetup = NewObject<UBodySetup>(Brush);
	FKBoxElem BoundsBox;
	BoundsBox.X = BoundsBox.Y = BoundsBox.Z = 100.0f;
	Brush->BrushBodySetup->AggGeom.BoxElems.Add(BoundsBox);
	Brush->BrushBodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
	Brush->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Brush->SetCollisionObjectType(ECC_WorldStatic);
	Brush->SetCollisionResponseToAllChannels(ECR_Block); // Custom profile, including WeaponAim.
	Brush->RegisterComponent();
	TestTrue(TEXT("Custom PCG brush is bounds"), ArrowCollisionQuery::IsPCGVolumeBounds(Brush));
	UBoxComponent* NamedBounds = PlayerArrowCollisionTests::AddBlockingBox(ProfileBounds, FVector(100, 0, 0));
	NamedBounds->SetCollisionProfileName(TEXT("PCGVolumeBounds"));
	PlayerArrowCollisionTests::AddBlockingBox(Wall, FVector(300, 0, 0));
	const FVector End(500, 0, 0);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ArrowPCGBoundsTest));
	Params.bFindInitialOverlaps = true;
	FHitResult Hit;
	TestTrue(TEXT("Raw query starts inside the Custom PCG brush"),
		Scope.World->SweepSingleByProfile(Hit, FVector::ZeroVector, End, FQuat::Identity, TEXT("ArrowObstacle"),
			FCollisionShape::MakeBox(FVector(8, 1, 1)), Params) && Hit.GetComponent() == Brush && Hit.bStartPenetrating);
	TestTrue(TEXT("Flight skips Custom and named bounds, then hits the real wall"),
		ArrowCollisionQuery::SweepFlight(*Arrow, FVector::ZeroVector, End, FQuat::Identity, Hit) && Hit.GetActor() == Wall);
	TestTrue(TEXT("Obstacle aim ray also reaches the wall"),
		ArrowCollisionQuery::TraceObstacles(Scope.World, FVector::ZeroVector, End, Params, Hit) && Hit.GetActor() == Wall);
	TestTrue(TEXT("WeaponAim skips Custom bounds even when they block its channel"),
		ArrowCollisionQuery::TraceAimTarget(Scope.World, FVector::ZeroVector, End, Params, Hit) && Hit.GetActor() == Wall);

	// Generated geometry may share its owner with the bounds. Never exclude that entire actor.
	UBoxComponent* GeneratedMeshCollision = NewObject<UBoxComponent>(Volume);
	Volume->AddInstanceComponent(GeneratedMeshCollision);
	GeneratedMeshCollision->SetBoxExtent(FVector(20));
	GeneratedMeshCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GeneratedMeshCollision->SetCollisionObjectType(ECC_WorldStatic);
	GeneratedMeshCollision->SetCollisionResponseToAllChannels(ECR_Block);
	GeneratedMeshCollision->RegisterComponent();
	GeneratedMeshCollision->SetWorldLocation(FVector(200, 0, 0));
	TestFalse(TEXT("PCG generated geometry is not bounds"), ArrowCollisionQuery::IsPCGVolumeBounds(GeneratedMeshCollision));
	TestTrue(TEXT("Flight still hits generated geometry owned by the same PCG volume"),
		ArrowCollisionQuery::SweepFlight(*Arrow, FVector::ZeroVector, End, FQuat::Identity, Hit)
		&& Hit.GetComponent() == GeneratedMeshCollision);
	GeneratedMeshCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Wall->Destroy();
	TestFalse(TEXT("Only bounds remain: flight passes through"),
		ArrowCollisionQuery::SweepFlight(*Arrow, FVector::ZeroVector, End, FQuat::Identity, Hit));
	return !HasAnyErrors();
}

#endif
