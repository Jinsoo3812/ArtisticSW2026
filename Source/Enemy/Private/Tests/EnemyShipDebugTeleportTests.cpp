#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING && !UE_BUILD_TEST

#include "Misc/AutomationTest.h"
#include "Development/SWEnemyShipDebugTeleport.h"
#include "ShipAI/EnemyShip.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyShipDebugTeleportRequesterTest,
	"ArtisticSW.Enemy.Ship.DebugTeleport.RequesterAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipDebugTeleportRequesterTest::RunTest(const FString& Parameters)
{
	// Existing crafting fixtures report these unrelated errors during world initialization.
	AddExpectedErrorPlain(TEXT("[ItemSubsystem][Crafting] QuestItem has an invalid ResultItemTag: Item.Quest.DecipheredCipher"),
		EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedErrorPlain(TEXT("[ItemSubsystem][Crafting] QuestItem contains an invalid ingredient."),
		EAutomationExpectedErrorFlags::Contains, 2);
	struct FScopedWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FScopedWorld()
		{
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}
		~FScopedWorld()
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	} TestWorld;
	UWorld* World = TestWorld.World;
	FSWEnemyShipDebugTeleport& Handler = SWEnemyShipDebug::GetTeleportHandler();
	if (!TestTrue(TEXT("Enemy module registered its teleport handler"), Handler.IsBound())) return false;

	AEnemyShip* Ship = World->SpawnActor<AEnemyShip>();
	APlayerController* FirstController = World->SpawnActor<APlayerController>();
	APlayerController* RequestingController = World->SpawnActor<APlayerController>();
	ACharacter* FirstPlayer = World->SpawnActor<ACharacter>(FVector(-10000, 0, 5000), FRotator::ZeroRotator);
	ACharacter* RequestingPlayer = World->SpawnActor<ACharacter>(FVector(-10000, 2000, 5000), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Ship"), Ship) || !TestNotNull(TEXT("First controller"), FirstController)
		|| !TestNotNull(TEXT("Requesting controller"), RequestingController)
		|| !TestNotNull(TEXT("First player"), FirstPlayer) || !TestNotNull(TEXT("Requesting player"), RequestingPlayer)) return false;
	FirstController->Possess(FirstPlayer);
	RequestingController->Possess(RequestingPlayer);
	Ship->Tags.Add(TEXT("DebugShip01"));
	USceneComponent* Arrival = NewObject<USceneComponent>(Ship);
	Ship->AddInstanceComponent(Arrival);
	Arrival->SetupAttachment(Ship->GetRootComponent());
	Arrival->ComponentTags.Add(TEXT("DebugArrival"));
	Arrival->RegisterComponent();
	Arrival->SetWorldLocationAndRotation(FVector(10000, 10000, 5000), FRotator(0, 70, 0));
	const FVector FirstLocation = FirstPlayer->GetActorLocation();
	RequestingPlayer->GetCharacterMovement()->Velocity = FVector(300, 200, -100);

	const FString Result = Handler.Execute(RequestingController, TEXT("DebugShip01"), TEXT("DebugArrival"));
	TestTrue(TEXT("Request succeeds"), Result.StartsWith(TEXT("Teleported")));
	TestTrue(TEXT("Requested player arrives"), RequestingPlayer->GetActorLocation().Equals(Arrival->GetComponentLocation(), 1.f));
	TestTrue(TEXT("First player's position is unchanged"), FirstPlayer->GetActorLocation().Equals(FirstLocation));
	TestTrue(TEXT("Old movement velocity cleared"), RequestingPlayer->GetVelocity().IsNearlyZero());
	TestEqual(TEXT("Landing resolves the new movement base"), RequestingPlayer->GetCharacterMovement()->MovementMode, MOVE_Falling);
	TestTrue(TEXT("Requested facing applied"), FMath::IsNearlyEqual(RequestingController->GetControlRotation().Yaw, 70.f));

	Arrival->SetWorldLocation(FVector(12000, 9000, 6000));
	Handler.Execute(RequestingController, TEXT("DebugShip01"), TEXT("DebugArrival"));
	TestTrue(TEXT("Destination is sampled on each request"), RequestingPlayer->GetActorLocation().Equals(Arrival->GetComponentLocation(), 1.f));
	const FVector BeforeRejectedRequest = RequestingPlayer->GetActorLocation();
	TestTrue(TEXT("Unknown ship is rejected"), Handler.Execute(RequestingController, TEXT("MissingShip"), TEXT("DebugArrival")).Contains(TEXT("found 0")));
	TestTrue(TEXT("Unknown arrival is rejected"), Handler.Execute(RequestingController, TEXT("DebugShip01"), TEXT("MissingArrival")).Contains(TEXT("found 0")));
	Ship->GetRootComponent()->ComponentTags.Add(TEXT("DebugArrival"));
	TestTrue(TEXT("Duplicate arrivals are rejected"), Handler.Execute(RequestingController, TEXT("DebugShip01"), TEXT("DebugArrival")).Contains(TEXT("found 2")));
	Ship->GetRootComponent()->ComponentTags.Remove(TEXT("DebugArrival"));
	RequestingPlayer->GetCharacterMovement()->DisableMovement();
	TestTrue(TEXT("Disabled movement is rejected"), Handler.Execute(RequestingController, TEXT("DebugShip01"), TEXT("DebugArrival")).Contains(TEXT("movable character")));
	TestTrue(TEXT("Rejected requests leave the pawn unchanged"), RequestingPlayer->GetActorLocation().Equals(BeforeRejectedRequest));
	TestTrue(TEXT("Other player remains untouched throughout"), FirstPlayer->GetActorLocation().Equals(FirstLocation));
	return true;
}

#endif
