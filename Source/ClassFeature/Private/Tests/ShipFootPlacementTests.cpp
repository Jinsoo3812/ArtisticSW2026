#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Animation/MotionMatchingAnimInstance.h"
#include "BasePlayer.h"
#include "CollisionChannels.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Settings_Item.h"
#include "Ship.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipFootPlacementSettingsTest,
    "ArtisticSW.Animation.FootPlacement.ShipContext",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipFootPlacementSettingsTest::RunTest(const FString& Parameters)
{
    // Keep this presentation fixture independent from quest recipe authoring.
    TGuardValue<TSoftObjectPtr<UDataTable>> CraftingTableGuard(GetMutableDefault<USettings_Item>()->CraftingRecipeDataTable, {});
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ShipFootPlacementTestWorld"));
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
    AShip* Ship = World->SpawnActor<AShip>(SpawnParams);
    AActor* Ground = World->SpawnActor<AActor>(SpawnParams);
    AActor* Equipment = World->SpawnActor<AActor>(SpawnParams);
    if (!TestNotNull(TEXT("Player"), Player) || !TestNotNull(TEXT("Ship"), Ship)
        || !TestNotNull(TEXT("Ground"), Ground) || !TestNotNull(TEXT("Equipment"), Equipment)) return false;
    Ship->BuoyancyRoot->SetSimulatePhysics(false);
    UBoxComponent* GroundRoot = NewObject<UBoxComponent>(Ground);
    Ground->SetRootComponent(GroundRoot);
    GroundRoot->RegisterComponent();
    USceneComponent* EquipmentRoot = NewObject<USceneComponent>(Equipment);
    Equipment->SetRootComponent(EquipmentRoot);
    EquipmentRoot->RegisterComponent();

    UMotionMatchingAnimInstance* Anim = NewObject<UMotionMatchingAnimInstance>(Player->GetMesh());
    Anim->CachedBasePlayer = Player;
    Anim->CachedLocomotionStateComponent = Player->FindComponentByClass<ULocomotionAnimStateComponent>();
    ULocomotionAnimStateComponent* State = Anim->CachedLocomotionStateComponent;
    if (!TestNotNull(TEXT("Locomotion state"), State)) return false;
    UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
    auto& Data = Anim->GetProxyOnGameThread<FMotionMatchingAnimInstanceProxy>().ThreadSafeData;

    // Deliberately different authored presets reveal accidental global mutation
    // or loss of the stop preset when toggling ship context.
    Anim->FootPlacementPlantSettingsDefault.LockType = EFootPlacementLockType::LockRotation;
    Anim->FootPlacementPlantSettingsDefault.UnplantRadius = 12.0f;
    Anim->FootPlacementPlantSettingsStops.LockType = EFootPlacementLockType::PivotAroundAnkle;
    Anim->FootPlacementPlantSettingsStops.UnplantRadius = 21.0f;
    Anim->FootPlacementInterpolationSettingsDefault.FloorAngularStiffness = 111.0f;
    Anim->FootPlacementInterpolationSettingsStops.FloorAngularStiffness = 222.0f;
    Data.FootPlacementAlpha = 0.75f;
    Data.LegIKAlpha = 0.9f;
    State->bStopRequested = false;

    Movement->SetMovementMode(MOVE_Walking);
    Player->SetBase(GroundRoot);
    Anim->UpdateFootPlacementSettings();
    TestFalse(TEXT("A nearby ship does not change land context"), Data.bIsOnShip);
    TestTrue(TEXT("Land preserves authored lock"), Anim->Get_FootPlacementPlantSettings().LockType == EFootPlacementLockType::LockRotation);
    TestEqual(TEXT("Land preserves authored radius"), Anim->Get_FootPlacementPlantSettings().UnplantRadius, 12.0f);
    TestEqual(TEXT("Land preserves interpolation"), Anim->Get_FootPlacementInterpolationSettings().FloorAngularStiffness, 111.0f);

    Anim->FootPlacementPlantSettingsDefault.UnplantRadius = 13.0f;
    TestEqual(TEXT("Graph reads the snapshot until the next GT update"), Anim->Get_FootPlacementPlantSettings().UnplantRadius, 12.0f);

    Player->SetBase(Ship->GetShipDeckMesh());
    Anim->UpdateFootPlacementSettings();
    TestTrue(TEXT("Walking on deck is ship context"), Data.bIsOnShip);
    TestTrue(TEXT("Ship releases only the plant lock"), Anim->Get_FootPlacementPlantSettings().LockType == EFootPlacementLockType::Unlocked);
    TestEqual(TEXT("Ship keeps the authored radius"), Anim->Get_FootPlacementPlantSettings().UnplantRadius, 13.0f);
    TestEqual(TEXT("Ship keeps slope extension limits"), Anim->Get_FootPlacementPlantSettings().MaxExtensionRatio, Anim->FootPlacementPlantSettingsDefault.MaxExtensionRatio);
    TestEqual(TEXT("Ship keeps floor interpolation"), Anim->Get_FootPlacementInterpolationSettings().FloorAngularStiffness, 111.0f);
    TestEqual(TEXT("Foot placement alpha stays active"), Data.FootPlacementAlpha, 0.75f);
    TestEqual(TEXT("Leg IK stays active"), Data.LegIKAlpha, 0.9f);

    // Exercise the entire normal frame, including the rebuilt proxy payload.
    // Testing the resolver alone misses a later default-constructed overwrite.
    Anim->HiddenRemoteUpdateInterval = 0.0f;
    Data.FootPlacementPlantSettings.LockType = EFootPlacementLockType::LockRotation;
    Data.MovementData.Velocity = FVector(999.0f, 0.0f, 0.0f);
    State->Velocity = FVector(123.0f, 0.0f, 0.0f);
    Anim->NativeUpdateAnimation(1.0f / 60.0f);
    TestEqual(TEXT("Full animation frame rebuilt the payload"), Data.MovementData.Velocity.X, 123.0);
    TestTrue(TEXT("Full animation frame preserves ship context"), Data.bIsOnShip);
    TestTrue(TEXT("Full animation frame preserves unlocked feet"), Anim->Get_FootPlacementPlantSettings().LockType == EFootPlacementLockType::Unlocked);
    TestEqual(TEXT("Full animation frame preserves authored interpolation"), Anim->Get_FootPlacementInterpolationSettings().FloorAngularStiffness, 111.0f);
    // The remaining resolver cases intentionally isolate alpha preservation.
    Data.FootPlacementAlpha = 0.75f;
    Data.LegIKAlpha = 0.9f;

    Player->SetBase(Ship->BuoyancyRoot);
    State->bStopRequested = true;
    Anim->UpdateFootPlacementSettings();
    TestTrue(TEXT("Attachment-root movement base is supported"), Data.bIsOnShip);
    TestTrue(TEXT("Stopping on ship also releases lock"), Anim->Get_FootPlacementPlantSettings().LockType == EFootPlacementLockType::Unlocked);
    TestEqual(TEXT("Ship stop keeps the stop preset"), Anim->Get_FootPlacementPlantSettings().UnplantRadius, 21.0f);
    TestEqual(TEXT("Ship stop keeps stop interpolation"), Anim->Get_FootPlacementInterpolationSettings().FloorAngularStiffness, 222.0f);

    // Helm possession clears movement/base. Ship association must survive via
    // attachment even though the player no longer owns a controller.
    Movement->DisableMovement();
    Player->SetBase(nullptr);
    Player->AttachToComponent(Ship->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
    Anim->UpdateFootPlacementSettings();
    TestTrue(TEXT("Attached helmsman with disabled movement is supported"), Data.bIsOnShip);
    TestTrue(TEXT("Helmsman releases the lock"), Anim->Get_FootPlacementPlantSettings().LockType == EFootPlacementLockType::Unlocked);

    Equipment->AttachToComponent(Ship->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
    Player->AttachToComponent(EquipmentRoot, FAttachmentTransformRules::KeepWorldTransform);
    Anim->UpdateFootPlacementSettings();
    TestTrue(TEXT("Nested ship equipment attachment is supported"), Data.bIsOnShip);

    Player->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    Movement->SetMovementMode(MOVE_Walking);
    Player->SetBase(GroundRoot);
    Anim->UpdateFootPlacementSettings();
    TestFalse(TEXT("Disembarking clears ship context immediately"), Data.bIsOnShip);
    TestTrue(TEXT("Disembarking restores the authored stop lock"), Anim->Get_FootPlacementPlantSettings().LockType == EFootPlacementLockType::PivotAroundAnkle);
    State->bStopRequested = false;
    Anim->UpdateFootPlacementSettings();
    TestTrue(TEXT("Land restores the default lock after stop"), Anim->Get_FootPlacementPlantSettings().LockType == EFootPlacementLockType::LockRotation);

    Movement->SetMovementMode(MOVE_Falling);
    Player->SetBase(Ship->BuoyancyRoot);
    Anim->UpdateFootPlacementSettings();
    TestFalse(TEXT("A stale ship base while falling is not standing on ship"), Data.bIsOnShip);

    Player->AttachToComponent(EquipmentRoot, FAttachmentTransformRules::KeepWorldTransform);
    Equipment->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    Anim->UpdateFootPlacementSettings();
    TestFalse(TEXT("Equipment no longer on ship clears nested context"), Data.bIsOnShip);
    TestTrue(TEXT("Default authored preset was never globally unlocked"), Anim->FootPlacementPlantSettingsDefault.LockType == EFootPlacementLockType::LockRotation);
    TestTrue(TEXT("Stop authored preset was never globally unlocked"), Anim->FootPlacementPlantSettingsStops.LockType == EFootPlacementLockType::PivotAroundAnkle);

    // Foot traces may hit only walkable geometry, not damage/interaction helpers.
    for (const FName Profile : { FName("Interactable"), FName("PlayerShipDamage"), FName("EnemyShipDamage") })
    {
        GroundRoot->SetCollisionProfileName(Profile);
        TestTrue(FString::Printf(TEXT("%s ignores foot queries"), *Profile.ToString()),
            GroundRoot->GetCollisionResponseToChannel(ECC_FootPlacement) == ECR_Ignore);
    }
    GroundRoot->SetCollisionProfileName(TEXT("ShipDeck"));
    TestTrue(TEXT("Ship deck blocks foot queries"), GroundRoot->GetCollisionResponseToChannel(ECC_FootPlacement) == ECR_Block);
    TestTrue(TEXT("Ship deck retains Pawn blocking"), GroundRoot->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerFootPlacementGraphContractTest,
    "ArtisticSW.Animation.FootPlacement.PlayerGraphContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerFootPlacementGraphContractTest::RunTest(const FString& Parameters)
{
    // Native ship settings cannot correct a graph that compensates platform
    // movement in world space or traces an unrelated collision channel.
    for (const TCHAR* Path : {
        TEXT("/Game/Anim_Logic/ABP_Player_Woman.ABP_Player_Woman_C"),
        TEXT("/Game/Anim_Logic/ABP_Player_Man.ABP_Player_Man_C")})
    {
        UClass* Class = LoadObject<UClass>(nullptr, Path);
        if (!TestNotNull(FString::Printf(TEXT("Animation class: %s"), Path), Class)) continue;
        TestTrue(FString::Printf(TEXT("Shared native animation parent: %s"), Path),
            Class->IsChildOf(UMotionMatchingAnimInstance::StaticClass()));

        int32 NodeCount = 0;
        for (TFieldIterator<FStructProperty> It(Class); It; ++It)
        {
            if (It->Struct != FAnimNode_FootPlacement::StaticStruct()) continue;
            const auto* Node = It->ContainerPtrToValuePtr<FAnimNode_FootPlacement>(Class->GetDefaultObject());
            const FString Label = FString::Printf(TEXT("%s/%s"), Path, *It->GetName());
            TestTrue(Label + TEXT(" uses component-space platform compensation"),
                Node->PelvisSettings.ActorMovementCompensationMode == EActorMovementCompensationMode::ComponentSpace);
            TestTrue(Label + TEXT(" traces FootPlacement for simple geometry"),
                UEngineTypes::ConvertToCollisionChannel(Node->TraceSettings.SimpleTraceChannel) == ECC_FootPlacement);
            TestTrue(Label + TEXT(" traces FootPlacement for complex geometry"),
                UEngineTypes::ConvertToCollisionChannel(Node->TraceSettings.ComplexTraceChannel) == ECC_FootPlacement);
            ++NodeCount;
        }
        TestTrue(FString::Printf(TEXT("Foot Placement node exists: %s"), Path), NodeCount > 0);
    }
    return !HasAnyErrors();
}

#endif
