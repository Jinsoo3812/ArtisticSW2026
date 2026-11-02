#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/SWCameraSpringArmComponent.h"
#include "CollisionChannels.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Misc/AutomationTest.h"

namespace CameraObstaclePolicyTests
{
	struct FWorldScope
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldScope() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
		~FWorldScope() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	};

	UBoxComponent* AddBox(AActor* Actor, const FVector& Position)
	{
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->AddInstanceComponent(Box);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(30.0));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ECC_WorldDynamic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		Actor->SetActorLocation(Position);
		return Box;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraPassThroughWallTest,
	"ArtisticSW.Camera.ObstaclePolicy.WallBehindIgnoredPillar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCameraPassThroughWallTest::RunTest(const FString& Parameters)
{
	CameraObstaclePolicyTests::FWorldScope Scope;
	AActor* Viewer = Scope.World->SpawnActor<AActor>();
	AActor* Pillar = Scope.World->SpawnActor<AActor>();
	AActor* Wall = Scope.World->SpawnActor<AActor>();
	if (!Viewer || !Pillar || !Wall) return false;
	UBoxComponent* PillarBox = CameraObstaclePolicyTests::AddBox(Pillar, FVector(-100, 0, 0));
	UBoxComponent* WallBox = CameraObstaclePolicyTests::AddBox(Wall, FVector(-300, 0, 0));
	PillarBox->ComponentTags.Add(TEXT("SW.Camera.Ignore"));
	USWCameraSpringArmComponent* Arm = NewObject<USWCameraSpringArmComponent>(Viewer);
	Viewer->AddInstanceComponent(Arm);
	Viewer->SetRootComponent(Arm);
	Arm->TargetArmLength = 400.f;
	Arm->ProbeSize = 12.f;
	Arm->RegisterComponent();
	const auto Update = [Arm]() { Arm->TickComponent(1.f / 60.f, LEVELTICK_All, nullptr); };
	Update();
	TestTrue(TEXT("Ignoring a foreground pillar still finds the wall behind it"),
		Arm->LastBlockingComponent.Get() == WallBox);
	TestTrue(TEXT("Camera passes pillar but stays in front of wall"),
		Arm->GetSocketLocation(USpringArmComponent::SocketName).X < -150.0
		&& Arm->GetSocketLocation(USpringArmComponent::SocketName).X > -270.0);
	TestTrue(TEXT("Native collision state includes the filtered wall"), Arm->IsCollisionFixApplied());

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CameraPolicyGameplayRegression), false, Viewer);
	for (ECollisionChannel Channel : { ECC_Pawn, ECC_Arrow, ECC_WeaponAim })
	{
		TestTrue(TEXT("Movement/arrow/aim queries still hit the ignored pillar"),
			Scope.World->LineTraceSingleByChannel(Hit, FVector::ZeroVector, FVector(-400, 0, 0), Channel, Params)
			&& Hit.GetComponent() == PillarBox);
	}

	Arm->MaxPolicySweeps = 1;
	Update();
	TestTrue(TEXT("Budget exhaustion stops at the pillar instead of crossing an untested wall"),
		Arm->bLastSweepBudgetExhausted && Arm->LastBlockingComponent.Get() == PillarBox);
	Arm->MaxPolicySweeps = 8;
	Arm->bEnableObstaclePolicy = false;
	Update();
	TestTrue(TEXT("Rollback restores native pillar collision"),
		Arm->GetSocketLocation(USpringArmComponent::SocketName).X > -100.0);
	Arm->bEnableObstaclePolicy = true;
	WallBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Update();
	TestTrue(TEXT("Clear path returns to full length and clears native fixed flag"),
		Arm->GetSocketLocation(USpringArmComponent::SocketName).Equals(FVector(-400, 0, 0), 0.1)
		&& !Arm->IsCollisionFixApplied());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraCharacterPolicyTest,
	"ArtisticSW.Camera.ObstaclePolicy.CharactersEquipmentAndBossOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCameraCharacterPolicyTest::RunTest(const FString& Parameters)
{
	CameraObstaclePolicyTests::FWorldScope Scope;
	ACharacter* Character = Scope.World->SpawnActor<ACharacter>();
	APawn* ShipLikePawn = Scope.World->SpawnActor<APawn>();
	AActor* Equipment = Scope.World->SpawnActor<AActor>();
	if (!Character || !ShipLikePawn || !Equipment) return false;
	UBoxComponent* CharacterBox = NewObject<UBoxComponent>(Character);
	UBoxComponent* ShipBox = NewObject<UBoxComponent>(ShipLikePawn);
	UBoxComponent* EquipmentBox = NewObject<UBoxComponent>(Equipment);
	Equipment->SetOwner(Character);
	const auto Ignore = [](const UPrimitiveComponent* Component)
	{
		return USWCameraSpringArmComponent::ShouldIgnoreCameraComponent(Component, true);
	};
	TestTrue(TEXT("Player/enemy character is pass-through"), Ignore(CharacterBox));
	TestTrue(TEXT("Character-owned equipment is pass-through"), Ignore(EquipmentBox));
	TestFalse(TEXT("Ships are not ignored simply because they are Pawns"), Ignore(ShipBox));
	TestFalse(TEXT("Character default can be disabled"),
		USWCameraSpringArmComponent::ShouldIgnoreCameraComponent(CharacterBox, false));
	Character->Tags.Add(TEXT("SW.Camera.Block"));
	TestFalse(TEXT("Boss actor override blocks"), Ignore(CharacterBox));
	TestFalse(TEXT("Boss override also reaches owned equipment"), Ignore(EquipmentBox));
	EquipmentBox->ComponentTags.Add(TEXT("SW.Camera.Ignore"));
	TestTrue(TEXT("Specific equipment component can override boss default"), Ignore(EquipmentBox));
	EquipmentBox->ComponentTags.Add(TEXT("SW.Camera.Block"));
	TestFalse(TEXT("Conflicting component tags keep blocking"), Ignore(EquipmentBox));
	return !HasAnyErrors();
}

#endif
