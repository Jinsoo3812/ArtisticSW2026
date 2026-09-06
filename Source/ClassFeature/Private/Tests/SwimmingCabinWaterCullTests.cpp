#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SwimmingComponent.h"
#include "SWCabinWaterCullComponent.h"
#include "SWCabinWaterCullData.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwimmingCabinMaskSamplingTest,
	"ArtisticSW.Swimming.CabinMaskSampling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwimmingCabinMaskSamplingTest::RunTest(const FString& Parameters)
{
	USWCabinWaterCullData* Data = NewObject<USWCabinWaterCullData>();
	Data->LocalBoundsMin = FVector::ZeroVector;
	Data->LocalBoundsMax = FVector(200, 100, 100);
	Data->Resolution = FIntVector(2, 1, 1);
	Data->OccupancyVoxels = {255, 0};
	TestTrue(TEXT("Occupied voxel center"), Data->ContainsLocalPosition(FVector(50, 50, 50)));
	TestFalse(TEXT("Empty voxel inside bounds is not cabin"), Data->ContainsLocalPosition(FVector(150, 50, 50)));
	TestTrue(TEXT("Linear filter includes 36 percent occupancy"), Data->ContainsLocalPosition(FVector(114, 50, 50)));
	TestFalse(TEXT("Linear filter excludes 34 percent occupancy"), Data->ContainsLocalPosition(FVector(116, 50, 50)));
	TestFalse(TEXT("Uses supplied material threshold"), Data->ContainsLocalPosition(FVector(114, 50, 50), 0.4f));
	TestTrue(TEXT("Texture clamps at bounds"), Data->ContainsLocalPosition(FVector(0, 0, 0)));
	TestFalse(TEXT("No padding outside bounds"), Data->ContainsLocalPosition(FVector(-0.01, 50, 50)));
	Data->OccupancyVoxels.Reset();
	TestFalse(TEXT("Missing CPU data does not suppress swimming"), Data->ContainsLocalPosition(FVector(50, 50, 50)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwimmingCabinFeetIntegrationTest,
	"ArtisticSW.Swimming.CabinFeetIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwimmingCabinFeetIntegrationTest::RunTest(const FString& Parameters)
{
	USWCabinWaterCullData* Data = LoadObject<USWCabinWaterCullData>(nullptr,
		TEXT("/Game/Blueprints/Water/Culling/DA_SW_ShipCabinWaterCull.DA_SW_ShipCabinWaterCull"));
	if (!TestNotNull(TEXT("Existing baked cabin data loads"), Data)
		|| !TestEqual(TEXT("Legacy texture migrated to CPU data"), int64(Data->OccupancyVoxels.Num()),
			int64(Data->Resolution.X) * Data->Resolution.Y * Data->Resolution.Z))
	{
		return false;
	}
	// The highest occupied voxel has an empty space above it: this distinguishes
	// a feet query from a capsule-center query using the actual shipped mask.
	const int32 Index = Data->OccupancyVoxels.FindLast(255);
	if (!TestTrue(TEXT("Mask contains occupied voxels"), Index != INDEX_NONE))
	{
		return false;
	}
	const FIntVector R = Data->Resolution;
	const FVector Cell(Index % R.X + 0.5, (Index / R.X) % R.Y + 0.5, Index / (R.X * R.Y) + 0.5);
	const FVector LocalFeet = Data->LocalBoundsMin + Cell / FVector(R) * (Data->LocalBoundsMax - Data->LocalBoundsMin);
	// EditorPreview avoids initializing unrelated game-instance subsystems while
	// still exercising actor/component registration and BeginPlay behavior.
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false, TEXT("SwimmingCabinTestWorld"));
	if (!TestNotNull(TEXT("World created"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
	AActor* CabinOwner = World->SpawnActor<AActor>();
	USceneComponent* Root = NewObject<USceneComponent>(CabinOwner);
	CabinOwner->SetRootComponent(Root);
	Root->RegisterComponent();
	USWCabinWaterCullComponent* Cabin = NewObject<USWCabinWaterCullComponent>(CabinOwner);
	Cabin->RegisterComponent();
	CabinOwner->DispatchBeginPlay();
	const FTransform Transform(FRotator(0, 31, 0), FVector(8000, 4000, 2000), FVector(1.1, 0.9, 1.2));
	CabinOwner->SetActorTransform(Transform);
	const FVector WorldFeet = Transform.TransformPosition(LocalFeet);
	TestTrue(TEXT("Translated rotated scaled cabin contains feet without local viewer"),
		USWCabinWaterCullComponent::IsWorldPositionInsideAnyCabin(World, WorldFeet));
	TestFalse(TEXT("Capsule center is outside mask"), Cabin->ContainsWorldPosition(WorldFeet + FVector(0, 0, 200)));
	ACharacter* Character = World->SpawnActor<ACharacter>();
	Character->GetCapsuleComponent()->SetCapsuleSize(34, 200);
	Character->SetActorLocation(WorldFeet + FVector(0, 0, 200));
	USwimmingComponent* Swim = NewObject<USwimmingComponent>(Character);
	Swim->RegisterComponent();
	Character->DispatchBeginPlay();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Movement->SetMovementMode(MOVE_Custom, uint8(ECustomMovementMode::CMOVE_Swimming));
	Swim->CheckWaterTransitions(0);
	TestFalse(TEXT("Feet entering cabin stops existing swimming without floor contact"), Swim->IsCustomSwimming());
	TestFalse(TEXT("Cabin does not impose shallow water slowdown"), Swim->IsInShallowWater());
	Cabin->bWaterCullEnabled = false;
	TestFalse(TEXT("Disabled cabin does not protect feet"),
		USWCabinWaterCullComponent::IsWorldPositionInsideAnyCabin(World, WorldFeet));
	Movement->SetMovementMode(MOVE_Custom, uint8(ECustomMovementMode::CMOVE_Swimming));
	Swim->CheckWaterTransitions(0);
	TestTrue(TEXT("Disabling cull releases gate to normal water-query grace"), Swim->IsCustomSwimming());
	Cabin->bWaterCullEnabled = true;
	Swim->CheckWaterTransitions(0);
	TestFalse(TEXT("Reentering cabin suppresses swimming again"), Swim->IsCustomSwimming());
	Character->SetActorLocation(Transform.TransformPosition(Data->LocalBoundsMax + FVector(1000)));
	Movement->SetMovementMode(MOVE_Custom, uint8(ECustomMovementMode::CMOVE_Swimming));
	Swim->CheckWaterTransitions(0);
	TestTrue(TEXT("Leaving volume releases gate immediately"), Swim->IsCustomSwimming());
	CabinOwner->Destroy();
	TestFalse(TEXT("Destroyed cabin no longer protects feet"),
		USWCabinWaterCullComponent::IsWorldPositionInsideAnyCabin(World, WorldFeet));
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
