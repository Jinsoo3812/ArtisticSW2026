#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PlayerLifeTestWorld.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerDeathCameraTest,
	"ArtisticSW.Player.Death.CameraFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerDeathCameraTest::RunTest(const FString& Parameters)
{
	PlayerLifeTests::FWorld Fixture;
	const PlayerLifeTests::FLife Life = Fixture.SpawnLife();
	if (!Life.Controller || !Life.Player) return false;
	ABasePlayerController* Controller = Life.Controller;
	Controller->SetAsLocalPlayerController();
	Controller->LastOwnAlivePOV.Location = FVector(200, 300, 400);
	Controller->LastOwnAlivePOV.Rotation = FRotator(-20, 35, 0);
	Controller->LastOwnAlivePOV.FOV = 85;
	Controller->bHasOwnPOV = true;
	const FMinimalViewInfo AlivePOV = Controller->LastOwnAlivePOV;
	const bool SavedAutoCamera = Controller->bAutoManageActiveCameraTarget;
	Controller->UnPossess();
	FSWDeathFlowState Waiting;
	Waiting.Phase = ESWPersonalLifePhase::WaitingForRespawn;
	Waiting.WaitingGeneration = 1;
	Controller->SetDeathFlowState(Waiting);
	if (!TestNotNull(TEXT("Independent spectator fallback camera"), Controller->DeathCamera.Get())) return false;
	ACameraActor* Camera = Controller->DeathCamera;
	TestEqual(TEXT("Waiting view targets independent camera"), Controller->GetViewTarget(), static_cast<AActor*>(Camera));
	TestTrue(TEXT("Fallback captures last living location"), Camera->GetActorLocation().Equals(AlivePOV.Location));
	TestEqual(TEXT("Fallback preserves field of view"), Camera->GetCameraComponent()->FieldOfView, AlivePOV.FOV);
	TestTrue(TEXT("Waiting blocks move and look input"), Controller->IsMoveInputIgnored() && Controller->IsLookInputIgnored());
	Life.Player->SetActorLocation(FVector(900, 0, -500));
	Controller->LastOwnAlivePOV.Location = FVector(999, 999, 999);
	Controller->OnRep_DeathFlowState();
	TestTrue(TEXT("Repeated waiting state cannot recapture corpse movement"), Camera->GetActorLocation().Equals(AlivePOV.Location));
	Life.Player->Destroy();
	Controller->ApplyLocalDeathFlow();
	TestEqual(TEXT("Corpse expiry does not invalidate spectator camera"), Controller->GetViewTarget(), static_cast<AActor*>(Camera));
	ABasePlayer* Replacement = Fixture.SpawnPlayer();
	if (!Replacement) return false;
	Controller->Possess(Replacement);
	FSWDeathFlowState Alive = Waiting;
	Alive.Phase = ESWPersonalLifePhase::Alive;
	Controller->SetDeathFlowState(Alive);
	TestEqual(TEXT("Restored life returns camera to replacement"), Controller->GetViewTarget(), static_cast<AActor*>(Replacement));
	TestFalse(TEXT("Move input unlocks without duplicate counters"), Controller->IsMoveInputIgnored());
	TestFalse(TEXT("Look input unlocks without duplicate counters"), Controller->IsLookInputIgnored());
	TestEqual(TEXT("Original engine camera management restored"), Controller->bAutoManageActiveCameraTarget, SavedAutoCamera);
	const FProperty* State = FindFProperty<FProperty>(ABasePlayerController::StaticClass(), TEXT("DeathFlowState"));
	if (TestNotNull(TEXT("Unified replicated life state"), State))
		TestTrue(TEXT("Life state handles replication ordering"), State->HasAllPropertyFlags(CPF_Net | CPF_RepNotify));
	return !HasAnyErrors();
}
#endif
