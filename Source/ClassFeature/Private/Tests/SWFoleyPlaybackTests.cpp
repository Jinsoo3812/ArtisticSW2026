#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Settings_Item.h"
#include "Audio/SWFoleyAudioProfile.h"
#include "Components/SWFoleyComponent.h"
#include "System/SWFoleySubsystem.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Sound/SoundBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWFoleyPlaybackTest, "SW.Foley.AuthoredAnimationPlayback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWFoleyPlaybackTest::RunTest(const FString&)
{
    TGuardValue<TSoftObjectPtr<UDataTable>> CraftingGuard(GetMutableDefault<USettings_Item>()->CraftingRecipeDataTable, {});
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
    World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
    auto* Character = World->SpawnActor<ACharacter>();
    auto* Component = NewObject<USWFoleyComponent>(Character);
    Character->AddInstanceComponent(Component); Component->RegisterComponent();
    auto* Profile = LoadObject<USWFoleyAudioProfile>(nullptr, TEXT("/Game/Audio/Foley/DA_Mannequin_Foley"));
    auto* Asset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin"));
    auto* Clip = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/UEFN_Mannequin/Animations/Run/M_Neutral_Run_Loop_F"));
    if (!TestNotNull(TEXT("Shared authored profile"), Profile) || !TestNotNull(TEXT("GASP source mesh"), Asset) || !TestNotNull(TEXT("Migrated forward run"), Clip)) return false;
    const FPrimaryAssetId ProfileId = UAssetManager::Get().GetPrimaryAssetIdForPath(FSoftObjectPath(Profile));
    TestTrue(TEXT("Default profile is registered for packaged builds"), ProfileId.IsValid());
    TestTrue(TEXT("Default profile and its authored sounds are included in cooking"),
        UAssetManager::Get().GetPrimaryAssetRules(ProfileId).CookRule == EPrimaryAssetCookRule::AlwaysCook);
    // Preparation only: gameplay's event path continues to use shared asynchronous loading.
    for (const auto& Pair : Profile->Events) { Pair.Value.DefaultSound.LoadSynchronous(); }
    Component->ProfileOverride = Profile;
    auto* PC = World->SpawnActor<APlayerController>(); PC->SetAsLocalPlayerController();
    auto* PlayerState = World->SpawnActor<APlayerState>(); PlayerState->SetOwner(PC); PC->SetPlayerState(PlayerState);
    PC->Possess(Character);
    Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    auto* Mesh = Character->GetMesh(); Mesh->SetSkeletalMeshAsset(Asset);
    Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Mesh->PlayAnimation(Clip, true);
    Component->PrepareLocalAudio();
    auto* Service = World->GetSubsystem<USWFoleySubsystem>();
    if (!TestNotNull(TEXT("Listener-side Foley service"), Service)) return false;
    for (int32 Frame = 0; Frame < 120; ++Frame)
    {
        // Drive the real asset notify queue without a timer or synthetic contact cadence.
        Mesh->TickAnimation(1.f / 60.f, false);
        Mesh->RefreshBoneTransforms();
        Mesh->ConditionallyDispatchQueuedAnimEvents();
    }
    TestTrue(TEXT("Authored animation emits both native foot contacts into the runtime service"), Service->GetStats().Submitted >= 2);
    TestEqual(TEXT("All emitted contacts resolve to existing GASP sounds"), Service->GetStats().Unmapped, uint64(0));
    TestTrue(TEXT("Repeated contacts share the per-foot admission gate"), Service->GetStats().DuplicateRejected > 0);
    Component->Deactivate();
    const uint64 Before = Service->GetStats().Submitted;
    Mesh->TickAnimation(Clip->GetPlayLength(), false); Mesh->RefreshBoneTransforms(); Mesh->ConditionallyDispatchQueuedAnimEvents();
    TestEqual(TEXT("Inactive component ignores subsequent authored contacts"), Service->GetStats().Submitted, Before);
    return true;
}
#endif
