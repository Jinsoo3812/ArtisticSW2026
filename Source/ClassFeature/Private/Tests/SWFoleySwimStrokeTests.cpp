#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Components/SWFoleyComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Settings_Item.h"
#include "SwimmingComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWFoleySwimStrokeTest, "SW.Foley.SurfaceSwimAdmission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSWFoleySwimStrokeTest::RunTest(const FString&)
{
	TGuardValue<TSoftObjectPtr<UDataTable>> CraftingGuard(GetMutableDefault<USettings_Item>()->CraftingRecipeDataTable, {});
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Character = World->SpawnActor<ACharacter>();
	auto* Foley = NewObject<USWFoleyComponent>(Character); Character->AddInstanceComponent(Foley); Foley->RegisterComponent();
	auto* Swimming = NewObject<USwimmingComponent>(Character); Character->AddInstanceComponent(Swimming); Swimming->RegisterComponent();
	auto* Mesh = Character->GetMesh();
	Foley->bUseAnimationDrivenSurfaceStrokes = true;
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	TestFalse(TEXT("Walking rejects swim animation markers"), Foley->AdmitSurfaceStroke(Mesh, ESWFoleySide::Left, 1.));
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Custom, uint8(ECustomMovementMode::CMOVE_Swimming));
	TestTrue(TEXT("Fixture uses the production custom swimming state"), Swimming->IsCustomSwimming());
	TestFalse(TEXT("Modular followers cannot duplicate a catch"), Foley->AdmitSurfaceStroke(NewObject<USkeletalMeshComponent>(Character), ESWFoleySide::Left, 1.));
	TestFalse(TEXT("A missing side is invalid"), Foley->AdmitSurfaceStroke(Mesh, ESWFoleySide::None, 1.));
	TestTrue(TEXT("First left-hand catch is admitted"), Foley->AdmitSurfaceStroke(Mesh, ESWFoleySide::Left, 1.));
	TestFalse(TEXT("Same hand blended twice is one catch"), Foley->AdmitSurfaceStroke(Mesh, ESWFoleySide::Left, 1.01));
	TestFalse(TEXT("Cross-instance simultaneous catches cannot stack"), Foley->AdmitSurfaceStroke(Mesh, ESWFoleySide::Right, 1.01));
	TestTrue(TEXT("Alternating right-hand catch follows animation"), Foley->AdmitSurfaceStroke(Mesh, ESWFoleySide::Right, 1.5));
	TestTrue(TEXT("Next cycle left-hand catch is admitted"), Foley->AdmitSurfaceStroke(Mesh, ESWFoleySide::Left, 2.));
	Foley->Deactivate();
	TestFalse(TEXT("Deactivated presentation stays silent"), Foley->AdmitSurfaceStroke(Mesh, ESWFoleySide::Right, 3.));
	Foley->Activate(); Character->SetActorHiddenInGame(true);
	TestFalse(TEXT("Hidden pooled actors stay silent"), Foley->AdmitSurfaceStroke(Mesh, ESWFoleySide::Right, 3.));
	Character->SetActorHiddenInGame(false);
	TestTrue(TEXT("Visible reactivation can present a new catch"), Foley->AdmitSurfaceStroke(Mesh, ESWFoleySide::Right, 3.));
	Foley->LastFullWaterEntryTime = 4.;
	TestFalse(TEXT("Body entry is not doubled by an immediate catch"), Foley->AdmitSurfaceStroke(Mesh, ESWFoleySide::Left, 4.1));
	return true;
}
#endif
