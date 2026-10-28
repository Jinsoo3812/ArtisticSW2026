#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Settings_Item.h"
#include "Audio/SWFoleyAudioProfile.h"
#include "Components/SWFoleyComponent.h"
#include "System/SWFoleySubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundWave.h"
#include "Ship.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWFoleySurfaceTraceTest, "SW.Foley.SurfaceTraceAndSelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWFoleySurfaceTraceTest::RunTest(const FString&)
{
    TGuardValue<TSoftObjectPtr<UDataTable>> CraftingGuard(GetMutableDefault<USettings_Item>()->CraftingRecipeDataTable, {});
    // Authored materials exist before a game world's Chaos solver is initialized.
    // Match that lifetime so its query-material snapshot contains this fixture.
    auto* Material = NewObject<UPhysicalMaterial>(); Material->SurfaceType = SurfaceType1;
    Material->GetPhysicsMaterial();
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
    World->InitializeActorsForPlay(FURL());
    World->GetWorldSettings()->NotifyBeginPlay();
    auto* Character = World->SpawnActor<ACharacter>();
    auto* Component = NewObject<USWFoleyComponent>(Character);
    Character->AddInstanceComponent(Component); Component->RegisterComponent();
    Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Character->SetActorLocation(FVector(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10.f));
    auto* Floor = World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box); Floor->AddInstanceComponent(Box);
    Box->SetMobility(EComponentMobility::Static);
    Box->SetBoxExtent(FVector(500.f, 500.f, 10.f));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->SetPhysMaterialOverride(Material); Box->RegisterComponent();
    Box->RecreatePhysicsState();
    FHitResult FixtureHit;
    FCollisionQueryParams FixtureQuery(SCENE_QUERY_STAT(FoleyFixture), false, Character);
    FixtureQuery.bReturnPhysicalMaterial = true;
    const bool bHit = World->LineTraceSingleByChannel(FixtureHit, FVector(0,0,30), FVector(0,0,-70), ECC_Visibility, FixtureQuery);
    AddInfo(FString::Printf(TEXT("Surface fixture Hit=%d Component=%s Material=%s Position=%s Physics=%d"),
        bHit, *GetNameSafe(FixtureHit.GetComponent()), *GetNameSafe(FixtureHit.PhysMaterial.Get()),
        *Component->ResolveContactLocation(*NewObject<USWFoleyAudioProfile>(), ESWFoleySide::None).ToString(), World->GetPhysicsScene()!=nullptr));
    TestTrue(TEXT("Fixture floor is queryable"), bHit && FixtureHit.GetComponent() == Box);
    TestEqual(TEXT("Fixture returns assigned physical material"), FixtureHit.PhysMaterial.Get(), Material);

    // Audio-device/editor diagnostics may retain a submitted wave after Stop().
    // Its Outer chain must not retain the disposable fixture world into PIE.
    auto* Profile = NewObject<USWFoleyAudioProfile>();
    TestFalse(TEXT("Submitted fixture assets cannot retain the temporary world"), Profile->IsIn(World));
    auto* Default = NewObject<USoundWave>(Profile, TEXT("DefaultSurfaceContact"));
    auto* Override = NewObject<USoundWave>(Profile, TEXT("SurfaceOneContact"));
    FSWFoleySoundSet Definition;
    Definition.DefaultSound = Default;
    Definition.SurfaceSounds.Add(SurfaceType1, Override);
    Definition.SurfaceSounds.Add(SurfaceType2, TSoftObjectPtr<USoundBase>());
    Profile->Events.Add(SW::Foley::Walk, Definition);
    Component->ProfileOverride = Profile;
    auto* Service = World->GetSubsystem<USWFoleySubsystem>();
    if (!TestNotNull(TEXT("Client Foley service"), Service)) return false;
    Service->LoadedProfiles.Add(Profile->GetPathName(), Profile);
    Service->ListenerFrame = GFrameCounter; Service->ListenerPositions.Add(Character->GetActorLocation());
    USWFoleySubsystem::FRequest Request;
    Request.Component = Component; Request.Profile = Profile->GetPathName();
    Request.Event.Event = SW::Foley::Walk; Request.bLocal = true;
    Service->Execute(Request);
    TestEqual(TEXT("Real ground trace returns Physical Material surface"), Service->Stats.LastSurface, SurfaceType1);
    TestEqual(TEXT("Surface override reaches playback submission"), Service->Stats.LastSound, Override->GetPathName());
    TestEqual(TEXT("Only selected contact traces"), Service->Stats.SurfaceTraces, uint64(1));
    Material->SurfaceType = SurfaceType2;
    Service->Execute(Request);
    TestEqual(TEXT("Empty surface override reaches default playback"), Service->Stats.LastSound, Default->GetPathName());
    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Service->Execute(Request);
    TestEqual(TEXT("Trace miss has default surface"), Service->Stats.LastSurface, SurfaceType_Default);
    TestEqual(TEXT("Trace miss retains default sound"), Service->Stats.LastSound, Default->GetPathName());
    // Match the production deck: Pawn blocks but Visibility deliberately ignores.
    // Keep deferred construction so this contact fixture has no ship gameplay.
    auto* Ship = World->SpawnActorDeferred<AShip>(AShip::StaticClass(), FTransform::Identity);
    auto* Deck = NewObject<UBoxComponent>(Ship);
    Ship->AddInstanceComponent(Deck); Deck->SetBoxExtent(FVector(500, 500, 10));
    Deck->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Deck->SetCollisionResponseToAllChannels(ECR_Ignore);
    Deck->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block); Deck->RegisterComponent();
    Character->SetBase(Deck);
    Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    auto* Wood = NewObject<USoundWave>(Profile, TEXT("WoodDeckContact"));
    Profile->Events[SW::Foley::Walk].SurfaceSounds.Add(Profile->ShipDeckSurface, Wood);
    const uint64 TracesBeforeDeck = Service->Stats.SurfaceTraces;
    Service->Execute(Request);
    TestEqual(TEXT("Accepted ship base resolves wood even when Visibility misses"), Service->Stats.LastSurface, Profile->ShipDeckSurface.GetValue());
    TestEqual(TEXT("Ship contact selects the authored deck recording"), Service->Stats.LastSound, Wood->GetPathName());
    TestEqual(TEXT("Deck fallback adds no second collision query"), Service->Stats.SurfaceTraces, TracesBeforeDeck + 1);
    const uint64 PlayedBeforeFalling = Service->Stats.Played;
    Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    Service->Execute(Request);
    TestEqual(TEXT("An airborne character cannot emit a contact from a stale deck base"), Service->Stats.Played, PlayedBeforeFalling);
    Character->SetBase(nullptr);
    Service->Stop();
    return true;
}
#endif
