#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Animation/MotionMatchingAnimInstance.h"
#include "BasePlayer.h"
#include "Chooser.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Settings_Item.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStopGaitIntegrationTest,
    "ArtisticSW.Animation.Stop.CapturedGait",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStopGaitIntegrationTest::RunTest(const FString& Parameters)
{
    TGuardValue<TSoftObjectPtr<UDataTable>> CraftingTableGuard(GetMutableDefault<USettings_Item>()->CraftingRecipeDataTable, {});
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("StopGaitTestWorld"));
    if (!TestNotNull(TEXT("Transient world"), World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ABasePlayer* Player = World->SpawnActor<ABasePlayer>(SpawnParams);
    if (!TestNotNull(TEXT("Player"), Player)) return false;
    auto* State = Player->GetAnimStateComponent();
    auto* Movement = Player->GetCharacterMovement();
    UMotionMatchingAnimInstance* Anim = NewObject<UMotionMatchingAnimInstance>(Player->GetMesh());
    Anim->CachedBasePlayer = Player;
    Anim->CachedLocomotionStateComponent = State;
    Anim->StopChooserTable = LoadObject<UChooserTable>(nullptr,
        TEXT("/Game/Anim_Logic/Choosers/Relaxed/CHT_Player_Stop_Relaxed.CHT_Player_Stop_Relaxed"));
    if (!TestNotNull(TEXT("Authored Stop chooser"), Anim->StopChooserTable.Get())) return false;

    auto TickState = [&](float Delta)
    {
        World->DeltaTimeSeconds = Delta;
        State->TickComponent(Delta, LEVELTICK_All, nullptr);
    };
    auto SelectStop = [&](bool bSprint)
    {
        Anim->StateControllerPlaybackHoldState = EStateControllerPresentationState::None;
        Anim->CurrentMovementDirection = EMovementDirection::Forward;
        Anim->EvaluateStateControllerPlaybackHold(EStateControllerPresentationState::TransitionToStop);
        UAnimationAsset* Selected = Anim->StateControllerSelectedAnimation;
        TestNotNull(TEXT("Stop selects a real authored animation"), Selected);
        TestTrue(TEXT("Captured gait reaches the Chooser"),
            Anim->StateControllerGait == (bSprint ? EGaitIntent::Sprint : EGaitIntent::Run));
        if (Selected)
        {
            TestTrue(TEXT("Selected clip matches the moving gait"),
                Selected->GetName().Contains(bSprint ? TEXT("_Sprint_Stop_") : TEXT("_Run_Stop_")));
            if (bSprint)
                TestEqual(TEXT("Sprint keeps authored entry after the two empty rows"), Anim->StateControllerSelectedAnimationStartTime, 0.7f);
        }
        return Selected;
    };

    for (float FPS : {30.0f, 60.0f, 120.0f})
    {
        for (bool bShiftReleasedFirst : {false, true})
        {
            Player->SetRole(ROLE_Authority);
            Movement->SetMovementMode(MOVE_Walking);
            Movement->Velocity = FVector(600.0f, 0.0f, 0.0f);
            State->SetMoveInput(0.0f, 1.0f);
            State->SetSprinting(true);
            TickState(1.0f / FPS);
            TestTrue(TEXT("Moving ground frame remembers Sprint"), State->bLastGroundMoveWasSprinting);
            if (bShiftReleasedFirst) State->SetSprinting(false);
            State->ClearMoveInput();
            State->SetSprinting(false);
            TickState(1.0f / FPS);
            TestTrue(TEXT("Either same-frame release order captures Sprint Stop"), State->bStopRequested && State->bStopWasSprinting);
            UAnimationAsset* Selected = SelectStop(true);
            State->ConsumeStopPresentationRequest();
            Movement->Velocity = FVector::ZeroVector;
            auto& Data = Anim->GetProxyOnGameThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;
            Data.MovementData.Velocity = FVector::ZeroVector;
            Data.InputData.bHasMoveInput = false;
            Anim->EvaluateStateControllerPlaybackHold(EStateControllerPresentationState::TransitionToStop);
            TestTrue(TEXT("Consumed request and zero speed retain Sprint for the held Stop"), Anim->StateControllerGait == EGaitIntent::Sprint);
            TestTrue(TEXT("Held Stop does not reselect the player"), Anim->StateControllerSelectedAnimation == Selected);
            Anim->EvaluateStateControllerPlaybackHold(EStateControllerPresentationState::IdleLoop);
            TestTrue(TEXT("Leaving Stop releases its gait lock for Idle"), Anim->StateControllerGait == EGaitIntent::Walk);

            // Releasing Shift for an actual moving frame must become Run,
            // rather than keeping an episode-wide 'ever sprinted' flag.
            State->SetMoveInput(0.0f, 1.0f);
            Movement->Velocity = FVector(300.0f, 0.0f, 0.0f);
            TickState(1.0f / FPS);
            TestFalse(TEXT("New Run frame invalidates the previous Sprint history"), State->bLastGroundMoveWasSprinting);
            TestFalse(TEXT("Resumed input cancels the previous Stop request"), State->bStopRequested);
            State->ClearMoveInput();
            TickState(1.0f / FPS);
            TestFalse(TEXT("Run movement requests Run Stop"), State->bStopWasSprinting);
            SelectStop(false);
            State->ConsumeStopPresentationRequest();
        }
    }

    // A proxy may receive the final release snapshot without ever seeing the
    // intermediate sprint-input sample. Ground history travels in the same
    // existing snapshot; no animation RPC or velocity threshold is required.
    Player->SetRole(ROLE_SimulatedProxy);
    FReplicatedLocomotionState Snapshot;
    Snapshot.bHasMoveInput = true;
    Snapshot.MoveInput = FVector2D(0.0f, 1.0f);
    State->ApplyAuthoritativeSnapshot(Snapshot);
    TickState(1.0f / 60.0f);
    Snapshot.bHasMoveInput = false;
    Snapshot.MoveInput = FVector2D::ZeroVector;
    Snapshot.bLastGroundMoveWasSprinting = true;
    State->ApplyAuthoritativeSnapshot(Snapshot);
    TickState(1.0f / 60.0f);
    TestTrue(TEXT("Coalesced remote release preserves the authoritative Sprint Stop"), State->bStopWasSprinting);
    SelectStop(true);
    FReplicatedLocomotionState DifferentGait = Snapshot;
    DifferentGait.bLastGroundMoveWasSprinting = false;
    TestTrue(TEXT("Ground gait changes participate in snapshot dirty checking"), DifferentGait != Snapshot);

    State->bLandWasMoving = true;
    State->bLandWasSprinting = true;
    State->bLastGroundMoveWasSprinting = false;
    State->InterruptLandingForStop();
    TestTrue(TEXT("Land to Stop uses captured landing gait"), State->bStopWasSprinting);
    SelectStop(true);

    Player->SetRole(ROLE_Authority);
    Movement->SetMovementMode(MOVE_Falling);
    TickState(1.0f / 60.0f);
    TestFalse(TEXT("Airborne movement clears ground history"), State->bLastGroundMoveWasSprinting);
    TestFalse(TEXT("Airborne movement cannot leave a pending Stop"), State->bStopRequested);
    Movement->SetMovementMode(MOVE_Walking);
    TickState(0.2f);
    State->SetMoveInput(0.0f, 1.0f);
    State->SetSprinting(true);
    TickState(1.0f / 60.0f);
    Player->bIsAttacking = true;
    TickState(1.0f / 60.0f);
    TestFalse(TEXT("Action interruption clears previous ground gait"), State->bLastGroundMoveWasSprinting);
    TestFalse(TEXT("Action interruption cancels Stop"), State->bStopRequested);
    return !HasAnyErrors();
}

#endif
