#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/SWPlayerCameraZoomComponent.h"
#include "BaseGameplayTags.h"
#include "GameFramework/SpringArmComponent.h"
#include "PlayerLifeTestWorld.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerCameraZoomTest,
	"ArtisticSW.Camera.ManualZoom.BoundsReservationAndPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerCameraZoomTest::RunTest(const FString& Parameters)
{
	PlayerLifeTests::FWorld Fixture;
	const auto Life = Fixture.SpawnLife();
	if (!Life.Controller || !Life.Player) return false;
	Life.Controller->SetAsLocalPlayerController();
	Life.Controller->SetViewTarget(Life.Player);
	Life.Controller->bShowMouseCursor = false;
	auto* Zoom = Life.Player->GetCameraZoom();
	Zoom->InitializeDistance(400.f);
	TestEqual(TEXT("Default initialization does not mask later save restoration"), Life.Controller->LocalPreferredCameraDistance, 0.f);
	Zoom->RestoreDistance(550.f);
	TestEqual(TEXT("Initial saved distance is restored before any local preference exists"), Zoom->GetDistance(), 550.f);
	Zoom->SetPreferredDistance(400.f);
	TestFalse(TEXT("Camera retraction defaults off"), Life.Player->GetCameraBoom()->bDoCollisionTest);
	TestTrue(TEXT("Normal local wheel accepted"), Zoom->ApplyWheelInput(1.f));
	TestEqual(TEXT("Wheel up zooms in"), Zoom->GetDistance(), 350.f);
	for (int32 I = 0; I < 30; ++I) Zoom->ApplyWheelInput(1.f);
	TestEqual(TEXT("Repeated scroll stops at minimum"), Zoom->GetDistance(), 150.f);
	for (int32 I = 0; I < 30; ++I) Zoom->ApplyWheelInput(-1.f);
	TestEqual(TEXT("Repeated scroll stops at maximum"), Zoom->GetDistance(), 800.f);
	TestEqual(TEXT("Sprint also respects maximum"), Zoom->GetSprintDistance(400.f, 450.f), 800.f);
	Zoom->SetPreferredDistance(500.f);
	TestEqual(TEXT("Sprint uses an offset from preference"), Zoom->GetSprintDistance(400.f, 450.f), 550.f);
	TestEqual(TEXT("Sprint does not overwrite preference"), Zoom->GetDistance(), 500.f);
	UObject* Selection = NewObject<USWPlayerCameraZoomComponent>(Life.Player);
	UObject* OtherSelection = NewObject<USWPlayerCameraZoomComponent>(Life.Player);
	Zoom->ReserveWheel(Selection, true);
	Zoom->ReserveWheel(OtherSelection, true);
	Zoom->ReserveWheel(Selection, false);
	TestFalse(TEXT("Remaining wheel consumer still suppresses zoom"), Zoom->ApplyWheelInput(1.f));
	Zoom->ReserveWheel(OtherSelection, false);
	Life.Controller->bShowMouseCursor = true;
	TestFalse(TEXT("UI cursor suppresses zoom"), Zoom->ApplyWheelInput(1.f));
	Life.Controller->bShowMouseCursor = false;
	Life.Controller->SetIgnoreLookInput(true);
	TestFalse(TEXT("Blocked camera input suppresses zoom"), Zoom->ApplyWheelInput(1.f));
	Life.Controller->ResetIgnoreLookInput();
	TestEqual(TEXT("Suppressed wheel was not queued"), Zoom->GetDistance(), 500.f);
	Life.State->GetAbilitySystemComponent()->AddLooseGameplayTag(State_Aiming);
	TestFalse(TEXT("Aim owns the camera instead of wheel zoom"), Zoom->ApplyWheelInput(1.f));
	Life.State->GetAbilitySystemComponent()->RemoveLooseGameplayTag(State_Aiming);
	Life.State->GetAbilitySystemComponent()->AddLooseGameplayTag(State_Sniping);
	TestFalse(TEXT("Sniping owns the camera instead of wheel zoom"), Zoom->ApplyWheelInput(1.f));
	Life.State->GetAbilitySystemComponent()->RemoveLooseGameplayTag(State_Sniping);
	ABasePlayer* Replacement = Fixture.SpawnPlayer();
	Life.Controller->Possess(Replacement);
	Replacement->GetCameraZoom()->InitializeDistance(400.f);
	TestEqual(TEXT("Local preference survives pawn replacement"), Replacement->GetCameraZoom()->GetDistance(), 500.f);
	TestFalse(TEXT("Unpossessed pawn cannot react to wheel"), Zoom->ApplyWheelInput(1.f));
	Zoom->MinDistance = 900.f;
	Zoom->MaxDistance = 200.f;
	TestEqual(TEXT("Reversed bounds normalize safely"), Zoom->ClampDistance(500.f), 500.f);
	TestEqual(TEXT("Restored out-of-range value clamps"), Zoom->ClampDistance(10000.f), 900.f);
	return !HasAnyErrors();
}
#endif
