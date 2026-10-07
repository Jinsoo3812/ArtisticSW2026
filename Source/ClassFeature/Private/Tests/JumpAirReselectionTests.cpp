#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/JumpAirReselection.h"
#include "Animation/AnimSequence.h"
#include "Animation/MotionMatchingAnimInstance.h"
#include "BasePlayer.h"
#include "Chooser.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Settings_Item.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "UObject/UnrealType.h"
#include "BoneControllers/AnimNode_OrientationWarping.h"
#include "Animation/AnimRootMotionProvider.h"
#include "Animation/AnimCurveFilter.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/MemStack.h"

namespace
{
    FVector2D JumpDirection(float Angle)
    {
        const float Radians = FMath::DegreesToRadians(Angle);
        return FVector2D(FMath::Sin(Radians), FMath::Cos(Radians));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJumpAirDirectionStabilityTest,
    "ArtisticSW.Animation.JumpAir.DirectionStability",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJumpAirDirectionStabilityTest::RunTest(const FString& Parameters)
{
    EMovementDirection Selected = EMovementDirection::Forward;
    for (float Angle : {21.0f, 24.0f, 22.0f, 29.0f, 20.0f})
    {
        Selected = JumpAirReselection::ResolveDirection(JumpDirection(Angle), Selected);
        TestTrue(TEXT("Camera jitter does not repeatedly switch the forward row"), Selected == EMovementDirection::Forward);
    }
    Selected = JumpAirReselection::ResolveDirection(JumpDirection(40.0f), Selected);
    TestTrue(TEXT("An intentional right turn selects FR"), Selected == EMovementDirection::ForwardRight);
    Selected = JumpAirReselection::ResolveDirection(JumpDirection(25.0f), Selected);
    TestTrue(TEXT("Returning through the old boundary retains FR"), Selected == EMovementDirection::ForwardRight);
    TestTrue(TEXT("Clear return to forward changes the row"),
        JumpAirReselection::ResolveDirection(JumpDirection(5.0f), Selected) == EMovementDirection::Forward);
    TestTrue(TEXT("Yaw wrapping retains backward"),
        JumpAirReselection::ResolveDirection(JumpDirection(179.0f), EMovementDirection::Backward) == EMovementDirection::Backward);
    TestTrue(TEXT("Missing velocity does not invent a new row"),
        JumpAirReselection::ResolveDirection(FVector2D::ZeroVector, EMovementDirection::Left) == EMovementDirection::Left);
    for (uint8 Sector = 0; Sector < 8; ++Sector)
    {
        const auto Direction = static_cast<EMovementDirection>(Sector);
        TestTrue(TEXT("All eight sector centers map to the authored enum"),
            JumpAirReselection::ResolveDirection(JumpDirection(-45.0f * Sector), EMovementDirection::Forward) == Direction);
    }
    TestFalse(TEXT("Takeoff lead-in cannot be interrupted"), JumpAirReselection::CanReselect(0.02f, 1.0f, -1.0f, 0.15f));
    TestTrue(TEXT("The first air turn can happen after the lead-in"), JumpAirReselection::CanReselect(0.06f, 1.0f, -1.0f, 0.15f));
    TestFalse(TEXT("The actual takeoff blend is protected even for the first air turn"),
        JumpAirReselection::CanReselect(0.06f, 1.0f, 0.0f, 0.20f));
    TestFalse(TEXT("Rapid reversal cannot interrupt a 0.2-second blend at 0.08 seconds"),
        JumpAirReselection::CanReselect(0.30f, 1.0f, 0.21f, 0.20f));
    TestFalse(TEXT("Default policy does not interrupt the final 30 percent of a blend"),
        JumpAirReselection::CanReselect(0.37f, 1.0f, 0.21f, 0.20f));
    TestTrue(TEXT("Reversal is allowed when the player blend has completed"),
        JumpAirReselection::CanReselect(0.43f, 1.0f, 0.21f, 0.20f));
    TestTrue(TEXT("Project_J's 70 percent policy remains configurable"),
        JumpAirReselection::CanReselect(0.37f, 1.0f, 0.21f, 0.20f, 0.70f));
    TestFalse(TEXT("The end of the jump hands off without another reselection"),
        JumpAirReselection::CanReselect(0.90f, 1.0f, 0.21f, 0.20f));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJumpAirRapidDirectionPolicyTest,
    "ArtisticSW.Animation.JumpAir.RapidDirectionPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJumpAirRapidDirectionPolicyTest::RunTest(const FString& Parameters)
{
    for (float FPS : {30.0f, 60.0f, 120.0f})
    {
        JumpAirReselection::FDirectionCandidate Candidate;
        EMovementDirection Selected = EMovementDirection::Forward;
        TArray<EMovementDirection> Choices;
        float LastBlendStart = 0.0f;
        float LastBlendDuration = 0.20f;
        const float Delta = 1.0f / FPS;
        for (float Time = Delta; Time < 0.70f; Time += Delta)
        {
            const auto Requested = Time < 0.08f ? EMovementDirection::Left :
                (Time < 0.10f ? EMovementDirection::Right :
                (Time < 0.45f ? EMovementDirection::Backward : EMovementDirection::Forward));
            const bool bStable = Candidate.Update(Requested, Selected, true, Delta, 0.04f);
            if (bStable && JumpAirReselection::CanReselect(Time, 1.2f, LastBlendStart, LastBlendDuration))
            {
                TestTrue(TEXT("Successive player blends never overlap their protected lifetime"),
                    Time - LastBlendStart >= LastBlendDuration);
                LastBlendDuration = JumpAirReselection::ResolveBlendTime(Selected, Requested, 0.0f, 0.22f);
                LastBlendStart = Time;
                Selected = Requested;
                Choices.Add(Selected);
                Candidate.Reset();
            }
        }
        TestEqual(TEXT("Fast turn and reversal select only the two sustained targets at every frame rate"), Choices.Num(), 2);
        if (Choices.Num() == 2)
        {
            TestTrue(TEXT("A fast sweep skips transient side poses and selects backward"), Choices[0] == EMovementDirection::Backward);
            TestTrue(TEXT("Latest forward intent replaces backward without replaying stale side intent"), Choices[1] == EMovementDirection::Forward);
        }
    }
    TestEqual(TEXT("A normal side transition keeps the existing 0.15s default"),
        JumpAirReselection::ResolveBlendTime(EMovementDirection::Forward, EMovementDirection::Left, 0.0f, 0.22f), 0.15f);
    TestEqual(TEXT("An opposite-pose transition has a longer crossfade"),
        JumpAirReselection::ResolveBlendTime(EMovementDirection::Left, EMovementDirection::Right, 0.0f, 0.22f), 0.22f);
    TestEqual(TEXT("A longer authored blend is never shortened"),
        JumpAirReselection::ResolveBlendTime(EMovementDirection::Forward, EMovementDirection::Backward, 0.3f, 0.22f), 0.3f);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJumpAirPlaybackContinuityTest,
    "ArtisticSW.Animation.JumpAir.PlaybackContinuity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJumpAirPlaybackContinuityTest::RunTest(const FString& Parameters)
{
    UAnimSequence* Forward = NewObject<UAnimSequence>();
    UAnimSequence* Backward = NewObject<UAnimSequence>();
    TestTrue(TEXT("F/FL/FR sharing a clip retain the existing player"),
        JumpAirReselection::SelectPlayback(Forward, Forward, 0.3f, 0.2f, 1.2f).bKeepPlayback);
    TestTrue(TEXT("A missing chooser row retains the existing player"),
        JumpAirReselection::SelectPlayback(Forward, nullptr, 0.0f, 0.2f, 0.0f).bKeepPlayback);

    // Switch between rows with different authored entries. Elapsed remains
    // time since takeoff, never an absolute clip time carried into another row.
    const auto ToBackward = JumpAirReselection::SelectPlayback(Forward, Backward, 0.0f, 0.2f, 1.2f);
    TestFalse(TEXT("Different backward clip can replace the player"), ToBackward.bKeepPlayback);
    TestEqual(TEXT("Backward row resumes at the current phase"), ToBackward.StartTime, 0.2f);
    const auto ToForward = JumpAirReselection::SelectPlayback(Backward, Forward, 0.3f, 0.4f, 1.2f);
    TestEqual(TEXT("Forward row adds its 0.3-second authored entry"), ToForward.StartTime, 0.7f);
    TestTrue(TEXT("Hold duration is measured from the authored entry"), FMath::IsNearlyEqual(ToForward.PlayableDuration, 0.9f, KINDA_SMALL_NUMBER));
    const auto BackAgain = JumpAirReselection::SelectPlayback(Forward, Backward, 0.0f, 0.6f, 1.2f);
    TestEqual(TEXT("Repeated switches do not accumulate authored offsets"), BackAgain.StartTime, 0.6f);
    TestTrue(TEXT("An exhausted target is not clamped to its last pose"),
        JumpAirReselection::SelectPlayback(Backward, Forward, 0.3f, 0.6f, 0.8f).bKeepPlayback);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJumpAirChooserIntegrationTest,
    "ArtisticSW.Animation.JumpAir.AuthoredChooserContinuity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJumpAirChooserIntegrationTest::RunTest(const FString& Parameters)
{
    // This animation fixture must not load unrelated quest recipe authoring.
    TGuardValue<TSoftObjectPtr<UDataTable>> CraftingTableGuard(GetMutableDefault<USettings_Item>()->CraftingRecipeDataTable, {});
    UChooserTable* Chooser = LoadObject<UChooserTable>(nullptr,
        TEXT("/Game/Anim_Logic/Choosers/Relaxed/CHT_Player_InAir_Relaxed.CHT_Player_InAir_Relaxed"));
    if (!TestNotNull(TEXT("Project jump chooser"), Chooser)) return false;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("JumpAirChooserTestWorld"));
    if (!TestNotNull(TEXT("Test world"), World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->DeltaTimeSeconds = 1.0f / 60.0f;
    ABasePlayer* Player = World->SpawnActor<ABasePlayer>();
    if (!TestNotNull(TEXT("Test player"), Player))
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }
    UMotionMatchingAnimInstance* Anim = NewObject<UMotionMatchingAnimInstance>(Player->GetMesh());
    Anim->CachedBasePlayer = Player;
    Anim->CachedLocomotionStateComponent = Player->GetAnimStateComponent();
    Anim->InAirChooserTable = Chooser;
    // These direct clock checks bypass confirmation; the per-frame scenarios
    // below exercise the normal confirmation and protection defaults.
    Anim->StateControllerJumpAirDirectionConfirmationTime = 0.0f;
    auto* State = Anim->CachedLocomotionStateComponent.Get();
    State->bIsJumping = true;
    State->bIsInAir = true;
    State->bIsFallOffStart = false;
    State->bJumpStartWasMoving = true;
    State->JumpStartMoveDirection = FVector2D(0.0f, 1.0f);
    State->bIsSprinting = false;
    Player->GetCharacterMovement()->Velocity = FVector(300.0f, 0.0f, 100.0f);
    Anim->EvaluateStateControllerPlaybackHold(EStateControllerPresentationState::TransitionToJump);
    UAnimationAsset* Forward = Anim->StateControllerSelectedAnimation;
    TestNotNull(TEXT("Forward jump row selects a sequence"), Forward);
    const float Entry = Anim->StateControllerSelectedAnimationStartTime;
    TestEqual(TEXT("Authored forward row starts at 0.3 seconds"), Entry, 0.3f);
    const auto Foot = Anim->StateControllerOneShotFoot;
    const float OriginalDuration = Anim->StateControllerPlaybackHoldDuration;
    const float TakeoffBlendDuration = Anim->LastJumpAirBlendDuration;
    AddInfo(FString::Printf(TEXT("Forward jump: length=%.3f entry=%.3f takeoff blend=%.3f"),
        Forward ? Forward->GetPlayLength() : 0.0f, Entry, TakeoffBlendDuration));

    // Use the real table, including the F aliases. A live sprint flag change
    // must not change the gait or the chosen leading foot during this jump.
    State->bIsSprinting = true;
    float Elapsed = TakeoffBlendDuration + 0.02f;
    for (float Yaw : {45.0f, -45.0f, 0.0f})
    {
        Anim->StateControllerPlaybackHoldElapsed = Elapsed;
        Player->SetActorRotation(FRotator(0.0f, Yaw, 0.0f));
        Anim->EvaluateStateControllerPlaybackHold(EStateControllerPresentationState::TransitionToJump);
        TestTrue(TEXT("F/FL/FR preserve the authored forward sequence"), Anim->StateControllerSelectedAnimation == Forward);
        TestFalse(TEXT("An alias does not force a new Blend Stack player"), Anim->bStateControllerForceBlendStackOnNextUpdate);
        TestEqual(TEXT("An alias does not rewrite the start-time pin"), Anim->StateControllerSelectedAnimationStartTime, Entry);
        TestEqual(TEXT("An alias does not reset or shift the hold duration"), Anim->StateControllerPlaybackHoldDuration, OriginalDuration);
        TestEqual(TEXT("An alias does not restart the actual blend clock"), Anim->LastJumpAirBlendStartElapsed, 0.0f);
        TestEqual(TEXT("An alias does not change the actual blend duration"), Anim->LastJumpAirBlendDuration, TakeoffBlendDuration);
        TestTrue(TEXT("An alias advances the elapsed clock by one update"),
            FMath::IsNearlyEqual(Anim->StateControllerPlaybackHoldElapsed, Elapsed + World->GetDeltaSeconds(), KINDA_SMALL_NUMBER));
        TestTrue(TEXT("Direction turn keeps the takeoff foot"), Anim->StateControllerOneShotFoot == Foot);
        TestTrue(TEXT("Direction turn keeps the takeoff gait"), Anim->StateControllerGait == EGaitIntent::Run);
        TestTrue(TEXT("Held direction agrees with the selected row"),
            Anim->StateControllerMovementDirection == JumpAirReselection::ResolveDirection(JumpDirection(-Yaw), EMovementDirection::Forward));
        // Aliases may follow quickly because no real player was created.
        Elapsed += 0.05f;
    }

    // A fresh jump must still replay the same forward sequence normally.
    State->bIsSprinting = false;
    Player->SetActorRotation(FRotator::ZeroRotator);
    Anim->StateControllerPlaybackHoldState = EStateControllerPresentationState::None;
    Anim->EvaluateStateControllerPlaybackHold(EStateControllerPresentationState::TransitionToJump);
    TestTrue(TEXT("A new jump event replays the existing forward asset"), Anim->bStateControllerForceBlendStackOnNextUpdate);

    // Traverse rows with both 0.3s and 0s entries using real project clips.
    Elapsed = TakeoffBlendDuration + 0.02f;
    for (float Yaw : {-90.0f, 180.0f, 0.0f})
    {
        UAnimationAsset* Previous = Anim->StateControllerSelectedAnimation;
        Anim->StateControllerPlaybackHoldElapsed = Elapsed;
        Player->SetActorRotation(FRotator(0.0f, Yaw, 0.0f));
        Anim->EvaluateStateControllerPlaybackHold(EStateControllerPresentationState::TransitionToJump);
        TestTrue(TEXT("A different authored direction can select a different sequence"),
            Anim->StateControllerSelectedAnimation && Anim->StateControllerSelectedAnimation != Previous);
        const float ExpectedElapsed = Elapsed + World->GetDeltaSeconds();
        const float AuthoredEntry = Anim->StateControllerSelectedAnimationOutput.StartTime;
        TestTrue(TEXT("The new player resumes at authored entry plus elapsed"),
            FMath::IsNearlyEqual(Anim->StateControllerSelectedAnimationStartTime, AuthoredEntry + ExpectedElapsed, KINDA_SMALL_NUMBER));
        TestTrue(TEXT("Hold elapsed remains relative after every direction switch"),
            FMath::IsNearlyEqual(Anim->StateControllerPlaybackHoldElapsed, ExpectedElapsed, KINDA_SMALL_NUMBER));
        TestTrue(TEXT("Duration remains relative to the authored entry"),
            Anim->StateControllerSelectedAnimation && FMath::IsNearlyEqual(Anim->StateControllerPlaybackHoldDuration,
                Anim->StateControllerSelectedAnimation->GetPlayLength() - AuthoredEntry, KINDA_SMALL_NUMBER));
        TestFalse(TEXT("Asset pin changes need no additional forced Blend Stack request"), Anim->bStateControllerForceBlendStackOnNextUpdate);
        Elapsed = ExpectedElapsed + Anim->LastJumpAirBlendDuration + 0.02f;
    }

    Anim->StateControllerJumpAirDirectionConfirmationTime = 0.04f;
    for (bool bMouseTurn : {true, false})
    {
        for (float FPS : {30.0f, 60.0f, 120.0f})
        {
            World->DeltaTimeSeconds = 1.0f / FPS;
            Player->SetActorRotation(FRotator::ZeroRotator);
            Player->GetCharacterMovement()->Velocity = FVector(300.0f, 0.0f, 100.0f);
            Anim->StateControllerPlaybackHoldState = EStateControllerPresentationState::None;
            Anim->CurrentMovementDirection = EMovementDirection::Forward;
            Anim->EvaluateStateControllerPlaybackHold(EStateControllerPresentationState::TransitionToJump);
            TArray<EMovementDirection> Choices;
            for (float Time = World->GetDeltaSeconds(); Time < 0.65f; Time += World->GetDeltaSeconds())
            {
                // Fast F -> L -> R -> B sweep, then a brief R and a sustained
                // return to F during the protected backward blend.
                const float Angle = Time < 0.08f ? -90.0f :
                    (Time < 0.10f ? 90.0f : (Time < 0.35f ? 180.0f : (Time < 0.36f ? 90.0f : 0.0f)));
                if (bMouseTurn)
                {
                    Player->SetActorRotation(FRotator(0.0f, -Angle, 0.0f));
                }
                else
                {
                    Player->GetCharacterMovement()->Velocity = FRotator(0.0f, Angle, 0.0f).Vector() * 300.0f + FVector(0.0f, 0.0f, 100.0f);
                }
                UAnimationAsset* Previous = Anim->StateControllerSelectedAnimation;
                const float PreviousBlendStart = Anim->LastJumpAirBlendStartElapsed;
                const float PreviousBlendDuration = Anim->LastJumpAirBlendDuration;
                Anim->EvaluateStateControllerPlaybackHold(EStateControllerPresentationState::TransitionToJump);
                if (Anim->StateControllerSelectedAnimation != Previous)
                {
                    Choices.Add(Anim->StateControllerMovementDirection);
                    TestTrue(TEXT("Real chooser changes wait for the active player blend"),
                        Anim->LastJumpAirBlendStartElapsed - PreviousBlendStart + KINDA_SMALL_NUMBER >= PreviousBlendDuration);
                    TestFalse(TEXT("Rapid clip changes do not double-request Blend Stack"), Anim->bStateControllerForceBlendStackOnNextUpdate);
                }
            }
            AddInfo(FString::Printf(TEXT("Rapid %s turn at %.0f FPS: %d actual player changes"),
                bMouseTurn ? TEXT("mouse") : TEXT("keyboard"), FPS, Choices.Num()));
            TestEqual(TEXT("Real chooser selects only sustained backward and forward poses"), Choices.Num(), 2);
            if (Choices.Num() == 2)
            {
                TestTrue(TEXT("First sustained air target is backward"), Choices[0] == EMovementDirection::Backward);
                TestTrue(TEXT("Latest sustained air target is forward"), Choices[1] == EMovementDirection::Forward);
            }
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJumpAirBlendStackContractTest,
    "ArtisticSW.Animation.JumpAir.AuthoredBlendStackContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJumpAirBlendStackContractTest::RunTest(const FString& Parameters)
{
    for (const TCHAR* Path : {
        TEXT("/Game/Anim_Logic/ABP_Player_Woman.ABP_Player_Woman_C"),
        TEXT("/Game/Anim_Logic/ABP_Player_Man.ABP_Player_Man_C")})
    {
        UClass* Class = LoadObject<UClass>(nullptr, Path);
        if (!TestNotNull(TEXT("Authored animation class"), Class)) continue;
        int32 NodeCount = 0;
        for (TFieldIterator<FStructProperty> It(Class); It; ++It)
        {
            if (It->Struct == FAnimNode_BlendStack::StaticStruct())
            {
                const auto* Node = It->ContainerPtrToValuePtr<FAnimNode_BlendStack>(Class->GetDefaultObject());
                AddInfo(FString::Printf(TEXT("%s: MaxAnimationDeltaTime=%.3f BlendOption=%d"),
                    Path, Node->MaxAnimationDeltaTime, static_cast<int32>(Node->BlendOption)));
                TestTrue(TEXT("Persistent entry-time pins must not trigger automatic animation resynchronization"), Node->MaxAnimationDeltaTime < 0.0f);
                ++NodeCount;
            }
        }
        TestTrue(TEXT("Authored animation graph contains Blend Stack"), NodeCount > 0);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJumpAirWarpingPolicyTest,
    "ArtisticSW.Animation.JumpAir.WarpingRuntimePolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJumpAirWarpingPolicyTest::RunTest(const FString& Parameters)
{
    for (const TCHAR* Path : {
        TEXT("/Game/Anim_Logic/ABP_Player_Man.ABP_Player_Man_C"),
        TEXT("/Game/Anim_Logic/ABP_Player_Woman.ABP_Player_Woman_C")})
    {
        UClass* AnimClass = LoadObject<UClass>(nullptr, Path);
        if (!TestNotNull(TEXT("Actual graph class for air smoothing"), AnimClass)) continue;
        auto* Mesh = NewObject<USkeletalMeshComponent>();
        auto* Anim = NewObject<UMotionMatchingAnimInstance>(Mesh, AnimClass);
        auto& Proxy = Anim->GetProxyOnGameThread<FMotionMatchingAnimInstanceProxy>();
        Proxy.CacheNodes(Anim);
        TestTrue(TEXT("Graph contains orientation warping nodes"), !Proxy.CachedJumpAirWarpingNodes.IsEmpty());
        Proxy.ThreadSafeData.AirData.bIsInAir = true;
        Proxy.ApplyJumpAirWarpingPolicy(Anim, 1.0f / 120.0f);
        for (const auto& Info : Proxy.CachedJumpAirWarpingNodes)
        {
            const auto* Node = Info.NodeProperty->ContainerPtrToValuePtr<FAnimNode_OrientationWarping>(Anim);
            TestTrue(TEXT("Air warping has interpolation instead of immediate inversion"), Node->RotationInterpSpeed > 0.0f);
            TestEqual(TEXT("Backward compensation threshold remains authored"), Node->LocomotionAngleDeltaThreshold, 90.0f);
        }
        Proxy.CacheNodes(Anim); // Cache refresh must not lose the authored zero.
        Proxy.ThreadSafeData.AirData.bIsInAir = false;
        Proxy.ThreadSafeData.LandingData.bIsLanding = true;
        Proxy.ApplyJumpAirWarpingPolicy(Anim, 0.1f);
        Proxy.ThreadSafeData.LandingData.bIsLanding = false;
        Proxy.ApplyJumpAirWarpingPolicy(Anim, 0.1f);
        for (const auto& Info : Proxy.CachedJumpAirWarpingNodes)
            TestTrue(TEXT("Smoothing survives the landing visual tail"), Info.bOverrideApplied || Info.OriginalInterpSpeed > 0.0f);
        Proxy.ApplyJumpAirWarpingPolicy(Anim, 0.11f);
        for (const auto& Info : Proxy.CachedJumpAirWarpingNodes)
        {
            const auto* Node = Info.NodeProperty->ContainerPtrToValuePtr<FAnimNode_OrientationWarping>(Anim);
            TestEqual(TEXT("Ground settings restore the actual authored speed"), Node->RotationInterpSpeed, Info.OriginalInterpSpeed);
            TestFalse(TEXT("No active override remains on the ground"), Info.bOverrideApplied);
        }
        if (!Proxy.CachedJumpAirWarpingNodes.IsEmpty())
        {
            auto* Node = Proxy.CachedJumpAirWarpingNodes[0].NodeProperty->ContainerPtrToValuePtr<FAnimNode_OrientationWarping>(Anim);
            Node->RotationInterpSpeed = 15.0f;
            Proxy.ThreadSafeData.AirData.bIsInAir = true;
            Proxy.ApplyJumpAirWarpingPolicy(Anim, 0.01f);
            TestEqual(TEXT("Existing positive smoothing is preserved"), Node->RotationInterpSpeed, 15.0f);
        }
        Proxy.ThreadSafeData.AirData.bIsInAir = false;
        Proxy.ApplyJumpAirWarpingPolicy(Anim, 0.21f);
        for (const auto& Info : Proxy.CachedJumpAirWarpingNodes)
            TestFalse(TEXT("Returning to ground removes runtime overrides"), Info.bOverrideApplied);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJumpAirEngineWarpingBoundaryTest,
    "ArtisticSW.Animation.JumpAir.EngineWarpingBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJumpAirEngineWarpingBoundaryTest::RunTest(const FString& Parameters)
{
    // Evaluate the real UE node with real bone mappings. A forward root delta
    // and 89 -> 91 degree velocity reproduce the graph's 180-degree inversion.
    USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr,
        TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"));
    if (!TestNotNull(TEXT("Skeleton for real engine warping evaluation"), Asset)) return false;
    const auto* Provider = UE::Anim::IAnimRootMotionProvider::Get();
    if (!TestNotNull(TEXT("Root motion attribute provider"), Provider)) return false;
    auto* Mesh = NewObject<USkeletalMeshComponent>();
    Mesh->SetSkeletalMeshAsset(Asset);
    auto* Anim = NewObject<UMotionMatchingAnimInstance>(Mesh);
    struct FTestProxy : FAnimInstanceProxy
    {
        using FAnimInstanceProxy::FAnimInstanceProxy;
        using FAnimInstanceProxy::PreUpdate;
    };
    FTestProxy Proxy(Anim);
    TArray<FBoneIndexType> RequiredBones;
    for (int32 Index = 0; Index < Asset->GetRefSkeleton().GetNum(); ++Index) RequiredBones.Add(Index);
    Proxy.GetRequiredBones().InitializeTo(RequiredBones, UE::Anim::FCurveFilterSettings(), *Asset);
    for (float FPS : {30.0f, 60.0f, 120.0f})
    {
        const float Delta = 1.0f / FPS;
        for (float InterpSpeed : {0.0f, 10.0f})
        {
            FAnimNode_OrientationWarping Warp;
            Warp.Mode = EWarpingEvaluationMode::Graph;
            Warp.RotationInterpSpeed = InterpSpeed;
            Warp.LocomotionAngleDeltaThreshold = 90.0f;
            Warp.DistributedBoneOrientationAlpha = 0.5f;
            Warp.SpineBones.Add(FBoneReference(TEXT("spine_01")));
            Warp.IKFootRootBone = FBoneReference(TEXT("ik_foot_root"));
            Warp.IKFootBones.Add(FBoneReference(TEXT("ik_foot_l")));
            Warp.IKFootBones.Add(FBoneReference(TEXT("ik_foot_r")));
            Proxy.PreUpdate(Anim, Delta);
            Warp.Initialize_AnyThread(FAnimationInitializeContext(&Proxy));
            Warp.CacheBones_AnyThread(FAnimationCacheBonesContext(&Proxy));
            Warp.ActualAlpha = 1.0f;
            TestTrue(TEXT("Real skeleton's spine/IK mappings are valid"), Warp.IsValidToEvaluate(Asset->GetSkeleton(), Proxy.GetRequiredBones()));
            auto Evaluate = [&](float Angle)
            {
                FMemMark Mark(FMemStack::Get());
                Proxy.PreUpdate(Anim, Delta);
                FComponentSpacePoseContext Output(&Proxy);
                Output.ResetToRefPose();
                const float Radians = FMath::DegreesToRadians(Angle);
                Warp.LocomotionDirection = FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0.0f);
                Provider->SetRootMotion(FTransform(FVector(3.0f, 0.0f, 0.0f)), Output.CustomAttributes);
                TArray<FBoneTransform> Transforms;
                Warp.EvaluateSkeletalControl_AnyThread(Output, Transforms);
                return Output.Pose.GetComponentSpaceTransform(FCompactPoseBoneIndex(0)).GetRotation();
            };
            FQuat Previous = Evaluate(89.0f);
            FQuat Current = Evaluate(91.0f);
            const float FirstStep = FMath::RadiansToDegrees(Previous.AngularDistance(Current));
            AddInfo(FString::Printf(TEXT("Real UE warper at %.0f FPS, interp %.0f: 89 -> 91 boundary root step %.3f degrees"), FPS, InterpSpeed, FirstStep));
            if (InterpSpeed <= 0.0f)
                TestTrue(TEXT("Authored zero interpolation reproduces the 89-degree root pop"), FMath::IsNearlyEqual(FirstStep, 89.0f, 0.1f));
            else
            {
                TestTrue(TEXT("Air smoothing prevents the near-90-degree instantaneous root change"), FirstStep < 35.0f);
                Previous = Current;
                for (int32 Index = 0; Index < 20; ++Index)
                {
                    Current = Evaluate(Index % 2 ? 91.0f : 89.0f);
                    const float Step = FMath::RadiansToDegrees(Previous.AngularDistance(Current));
                    TestTrue(TEXT("Repeated boundary crossings remain smoothed"), Step < 35.0f);
                    Previous = Current;
                }
            }
        }
    }
    return !HasAnyErrors();
}

#endif
