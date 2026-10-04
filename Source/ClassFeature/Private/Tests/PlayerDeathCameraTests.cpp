#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "Camera/PlayerDeathCameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Settings_Item.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerDeathCameraTest,
	"ArtisticSW.Player.Death.CameraFollowsPhysics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerDeathCameraTest::RunTest(const FString& Parameters)
{
	TGuardValue<TSoftObjectPtr<UDataTable>> CraftingTableGuard(GetMutableDefault<USettings_Item>()->CraftingRecipeDataTable, {});
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("PlayerDeathCameraTest"));
	if (!TestNotNull(TEXT("Camera test world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL());
	ABasePlayer* Player = World->SpawnActor<ABasePlayer>();
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/jiwon/Characters/SKM_Player_Woman.SKM_Player_Woman"));
	if (!TestNotNull(TEXT("Player"), Player) || !TestNotNull(TEXT("Mesh"), Mesh)) return false;
	Player->GetMesh()->SetSkeletalMesh(Mesh);
	USpringArmComponent* Boom = Player->GetCameraBoom();
	UPlayerDeathCameraComponent* Camera = Player->GetDeathCameraComponent();
	if (!TestNotNull(TEXT("Death camera component"), Camera)) return false;
	const FTransform InitialRelativeTransform = Boom->GetRelativeTransform();
	const ETickingGroup InitialTickGroup = Boom->PrimaryComponentTick.TickGroup;
	const FVector CapsuleLocation = Player->GetActorLocation();
	const FQuat CameraRotation = Boom->GetComponentQuat();
	Player->ApplyLocalDeathRagdoll();
	TestTrue(TEXT("Ragdoll starts camera following"), Camera->IsFollowing());
	TestTrue(TEXT("SpringArm solves after physics"), Boom->PrimaryComponentTick.TickGroup == TG_PostPhysics);
	FBodyInstance* Pelvis = Player->GetMesh()->GetBodyInstance(TEXT("pelvis"));
	if (!TestNotNull(TEXT("Actual pelvis physics body"), Pelvis) || !Pelvis->IsValidBodyInstance()) return false;
	Camera->FollowInterpSpeed = 0.0f;
	FTransform BodyTransform = Pelvis->GetUnrealWorldTransform();
	BodyTransform.AddToTranslation(FVector(250.0f, -120.0f, -80.0f));
	BodyTransform.SetRotation(FQuat(FVector::ForwardVector, PI * 0.5f));
	Pelvis->SetBodyTransform(BodyTransform, ETeleportType::TeleportPhysics);
	Camera->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Pivot follows solved pelvis, independent of animation refresh"),
		Boom->GetComponentLocation().Equals(BodyTransform.GetLocation() + Camera->FocusOffset, 0.01f));
	TestTrue(TEXT("Camera tracking does not move the gameplay capsule"), Player->GetActorLocation().Equals(CapsuleLocation));
	TestTrue(TEXT("Body roll does not roll the camera"), Boom->GetComponentQuat().Equals(CameraRotation, 0.001f));
	Player->ApplyLocalDeathRagdoll(); // Repeated presentation must not overwrite the reset transform.
	Camera->FocusBone = TEXT("MissingFocusBone");
	Camera->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Missing focus bone has a finite mesh fallback"),
		Boom->GetComponentLocation().Equals(Player->GetMesh()->GetComponentLocation() + Camera->FocusOffset, 0.01f));
	Player->ResetLocalDeathRagdoll();
	TestFalse(TEXT("Reuse stops death tracking"), Camera->IsFollowing());
	TestFalse(TEXT("Reuse disables presentation tick"), Camera->IsComponentTickEnabled());
	TestTrue(TEXT("Reuse restores original camera transform"), Boom->GetRelativeTransform().Equals(InitialRelativeTransform));
	TestTrue(TEXT("Reuse restores SpringArm tick group"), Boom->PrimaryComponentTick.TickGroup == InitialTickGroup);
	const FProperty* Target = FindFProperty<FProperty>(ABasePlayerController::StaticClass(), TEXT("DeathViewTarget"));
	if (TestNotNull(TEXT("Controller death view state"), Target))
	{
		TestTrue(TEXT("Death view survives replication ordering"), Target->HasAllPropertyFlags(CPF_Net | CPF_RepNotify));
	}
	return !HasAnyErrors();
}

#endif
