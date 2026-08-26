#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "BasePlayer.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Equipment/WeaponAnimationDataAsset.h"
#include "UI/PlayerStatusPreviewStage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlayerStatusPreviewTest,
	"ArtisticSW.Status.PlayerPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerStatusPreviewTest::RunTest(const FString& Parameters)
{
	const TCHAR* BowDataPath = TEXT("/Game/Blueprints/DAs/Weapon_DAs/Bow/WDA_Bow.WDA_Bow");
	const TCHAR* SwordADataPath = TEXT("/Game/Blueprints/DAs/Weapon_DAs/Sword/WDA_SwordA.WDA_SwordA");
	const TCHAR* SwordBDataPath = TEXT("/Game/Blueprints/DAs/Weapon_DAs/Sword/WDA_SwordB.WDA_SwordB");
	const TCHAR* BowIdlePath = TEXT("/Game/Anim_Logic/Anim_Assets/Bow/Standing_Idle_01_Anim.Standing_Idle_01_Anim");
	const TCHAR* SwordIdlePath = TEXT("/Game/Sword_Anims/Animations/HandsomeSwordV2/Manny_UE5/RootMotion/Idle/Anim_SwordV2_Idle.Anim_SwordV2_Idle");

	UAnimSequenceBase* BowIdle = LoadObject<UAnimSequenceBase>(nullptr, BowIdlePath);
	UAnimSequenceBase* SwordIdle = LoadObject<UAnimSequenceBase>(nullptr, SwordIdlePath);
	UWeaponAnimationDataAsset* BowData = LoadObject<UWeaponAnimationDataAsset>(nullptr, BowDataPath);
	UWeaponAnimationDataAsset* SwordAData = LoadObject<UWeaponAnimationDataAsset>(nullptr, SwordADataPath);
	UWeaponAnimationDataAsset* SwordBData = LoadObject<UWeaponAnimationDataAsset>(nullptr, SwordBDataPath);

	if (!TestNotNull(TEXT("Relaxed bow idle loads"), BowIdle)
		|| !TestNotNull(TEXT("Sword idle loads"), SwordIdle)
		|| !TestNotNull(TEXT("Bow animation data loads"), BowData)
		|| !TestNotNull(TEXT("Sword A animation data loads"), SwordAData)
		|| !TestNotNull(TEXT("Sword B animation data loads"), SwordBData))
	{
		return false;
	}

	TestEqual(TEXT("Bow data uses the relaxed preview idle"),
		BowData->GetDefaultEntry().PreviewIdleAnimation.LoadSynchronous(), BowIdle);
	TestEqual(TEXT("Sword A data uses the sword preview idle"),
		SwordAData->GetDefaultEntry().PreviewIdleAnimation.LoadSynchronous(), SwordIdle);
	TestEqual(TEXT("Sword B data uses the sword preview idle"),
		SwordBData->GetDefaultEntry().PreviewIdleAnimation.LoadSynchronous(), SwordIdle);

	UWorld* PreviewWorld = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		TEXT("PlayerStatusPreviewTestWorld"));
	if (!TestNotNull(TEXT("Preview test world is created"), PreviewWorld))
	{
		return false;
	}

	APlayerStatusPreviewStage* Stage = PreviewWorld->SpawnActor<APlayerStatusPreviewStage>();
	TestNotNull(TEXT("Player preview stage spawns"), Stage);
	if (Stage)
	{
		UClass* PlayerClass = LoadClass<ABasePlayer>(
			nullptr,
			TEXT("/Game/Blueprints/Player/BP_Player.BP_Player_C"));
		ABasePlayer* Player = PlayerClass
			? PreviewWorld->SpawnActor<ABasePlayer>(PlayerClass)
			: nullptr;
		TestNotNull(TEXT("Configured player Blueprint loads"), PlayerClass);
		TestNotNull(TEXT("Configured player spawns for preview"), Player);
		if (Player)
		{
			Stage->SetSourcePlayer(Player);
		}

		USceneCaptureComponent2D* Capture = Stage->FindComponentByClass<USceneCaptureComponent2D>();
		USkeletalMeshComponent* PreviewMesh = Stage->FindComponentByClass<USkeletalMeshComponent>();
		UTextureRenderTarget2D* RenderTarget = Stage->GetRenderTarget();
		TestNotNull(TEXT("Preview stage owns a scene capture"), Capture);
		TestNotNull(TEXT("Preview stage owns a skeletal mesh"), PreviewMesh);
		TestNotNull(TEXT("Preview stage owns a render target"), RenderTarget);
		if (Player && PreviewMesh)
		{
			TestEqual(TEXT("Preview uses the player's current skeletal mesh"),
				PreviewMesh->GetSkeletalMeshAsset(), Player->GetMesh()->GetSkeletalMeshAsset());
			UAnimSingleNodeInstance* PreviewAnimation = PreviewMesh->GetSingleNodeInstance();
			TestNotNull(TEXT("Preview creates a single-node animation instance"), PreviewAnimation);
			if (PreviewAnimation)
			{
				TestNotNull(TEXT("Preview applies its configured unarmed idle"), PreviewAnimation->GetCurrentAsset());
			}
		}
		if (Capture)
		{
			TestFalse(TEXT("Closed Status preview does not capture every frame"), Capture->bCaptureEveryFrame);
			TestEqual(TEXT("Preview capture exports inverse opacity for UI composition"),
				Capture->CaptureSource, ESceneCaptureSource::SCS_SceneColorHDR);
			TestEqual(TEXT("Preview capture renders only explicitly selected player meshes"),
				Capture->PrimitiveRenderMode, ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList);
			if (PreviewMesh)
			{
				TestTrue(TEXT("Preview character is present in the ShowOnly list"),
					Capture->ShowOnlyComponents.Contains(PreviewMesh));
			}
		}
		if (RenderTarget)
		{
			TestTrue(TEXT("Preview render target is transient"), RenderTarget->HasAnyFlags(RF_Transient));
			TestEqual(TEXT("Preview render target width is 768"), RenderTarget->SizeX, 768);
			TestEqual(TEXT("Preview render target height is 1024"), RenderTarget->SizeY, 1024);
		}
	}

	PreviewWorld->DestroyWorld(false);
	return true;
}

#endif
