#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Facility/FacilityHubActor.h"
#include "UI/Crafting/CraftingPanelWidget.h"
#include "UI/FacilityHubWidget.h"
#include "UI/ShipUpgradeConnectionWidget.h"
#include "UI/ShipUpgradeDetailsWidget.h"
#include "UI/ShipUpgradeGraphWidget.h"
#include "UI/ShipUpgradeMaterialRowWidget.h"
#include "UI/ShipUpgradeNodeWidget.h"
#include "UI/ShipUpgradePreviewStage.h"
#include "UI/ShipUpgradeScreenWidget.h"
#include "UI/ShipUpgradeStatChangeRowWidget.h"
#include "UI/ShipUpgradeWorkspaceWidget.h"
#include "UObject/CoreRedirects.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShipUpgradeUIModulePlacementTest,
	"ArtisticSW.ShipUpgrade.UI.ModulePlacementAndRedirects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipUpgradeUIModulePlacementTest::RunTest(const FString& Parameters)
{
	struct FClassExpectation
	{
		const TCHAR* OldPath;
		UClass* NewClass;
	};

	const FClassExpectation Expectations[] =
	{
		{ TEXT("/Script/ClassFeature.ShipUpgradeConnectionWidget"), UShipUpgradeConnectionWidget::StaticClass() },
		{ TEXT("/Script/ClassFeature.ShipUpgradeDetailsWidget"), UShipUpgradeDetailsWidget::StaticClass() },
		{ TEXT("/Script/ClassFeature.ShipUpgradeGraphWidget"), UShipUpgradeGraphWidget::StaticClass() },
		{ TEXT("/Script/ClassFeature.ShipUpgradeMaterialRowWidget"), UShipUpgradeMaterialRowWidget::StaticClass() },
		{ TEXT("/Script/ClassFeature.ShipUpgradeNodeWidget"), UShipUpgradeNodeWidget::StaticClass() },
		{ TEXT("/Script/ClassFeature.ShipUpgradePreviewStage"), AShipUpgradePreviewStage::StaticClass() },
		{ TEXT("/Script/ClassFeature.ShipUpgradeScreenWidget"), UShipUpgradeScreenWidget::StaticClass() },
		{ TEXT("/Script/ClassFeature.ShipUpgradeStatChangeRowWidget"), UShipUpgradeStatChangeRowWidget::StaticClass() },
		{ TEXT("/Script/ClassFeature.ShipUpgradeWorkspaceWidget"), UShipUpgradeWorkspaceWidget::StaticClass() },
	};

	for (const FClassExpectation& Expectation : Expectations)
	{
		TestEqual(
			FString::Printf(TEXT("%s is exported by ShipUpgradeUI"), *Expectation.NewClass->GetName()),
			Expectation.NewClass->GetOutermost()->GetName(),
			FString(TEXT("/Script/ShipUpgradeUI")));

		const FCoreRedirectObjectName RedirectedName = FCoreRedirects::GetRedirectedName(
			ECoreRedirectFlags::Type_Class,
			FCoreRedirectObjectName(FString(Expectation.OldPath)));
		TestEqual(
			FString::Printf(TEXT("%s redirects to the relocated class"), Expectation.OldPath),
			RedirectedName.ToString(),
			FCoreRedirectObjectName(Expectation.NewClass).ToString());
	}

	TestTrue(
		TEXT("The existing ship workspace is a compatibility child of the single FacilityHub shell"),
		UShipUpgradeWorkspaceWidget::StaticClass()->IsChildOf(UFacilityHubWidget::StaticClass()));

	UClass* WorkspaceAssetClass = LoadClass<UFacilityHubWidget>(
		nullptr,
		TEXT("/Game/Blueprints/02_UI/UI_WorkTable/WBP_WorkspaceScreen.WBP_WorkspaceScreen_C"));
	TestNotNull(TEXT("The existing ship workspace design loads as a FacilityHub widget"), WorkspaceAssetClass);

	UClass* ShipUpgradeScreenAssetClass = LoadClass<UShipUpgradeScreenWidget>(
		nullptr,
		TEXT("/Game/Blueprints/02_UI/UI_WorkTable/UI_ShipUpgrade/WBP_ShipUpgradeScreen.WBP_ShipUpgradeScreen_C"));
	TestNotNull(TEXT("The ship upgrade screen design loads"), ShipUpgradeScreenAssetClass);
	if (const UWidgetBlueprintGeneratedClass* ScreenWidgetClass =
		Cast<UWidgetBlueprintGeneratedClass>(ShipUpgradeScreenAssetClass))
	{
		const UWidgetTree* ScreenTree = ScreenWidgetClass->GetWidgetTreeArchetype();
		TestNotNull(TEXT("The ship upgrade screen has a compiled widget tree"), ScreenTree);
		if (ScreenTree)
		{
			for (const FName WidgetName :
				{ FName(TEXT("GraphWidget")), FName(TEXT("DetailsWidget")),
					FName(TEXT("SizeBox_GraphExtent")), FName(TEXT("Image_MainShipPreview")) })
			{
				TestNotNull(
					FString::Printf(TEXT("The screen contains required widget %s"), *WidgetName.ToString()),
					ScreenTree->FindWidget(WidgetName));
			}
		}
	}

	UClass* CraftingPanelAssetClass = LoadClass<UCraftingPanelWidget>(
		nullptr,
		TEXT("/Game/Blueprints/02_UI/UI_FacilityHub/Crafting/WBP_CraftingPanel.WBP_CraftingPanel_C"));
	TestNotNull(TEXT("The teammate crafting panel remains loadable"), CraftingPanelAssetClass);

	UClass* FacilityActorAssetClass = LoadClass<AFacilityHubActor>(
		nullptr,
		TEXT("/Game/Blueprints/03_WorldObject/03_FacilityHub/BP_FacilityHub.BP_FacilityHub_C"));
	TestNotNull(TEXT("The teammate FacilityHub actor remains loadable"), FacilityActorAssetClass);

	UMaterialInterface* PreviewOverlayMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Blueprints/02_UI/UI_WorkTable/UI_ShipUpgrade/M_ShipPreviewOverlay.M_ShipPreviewOverlay"));
	TestNotNull(
		TEXT("The transparent ship preview overlay material loads"),
		PreviewOverlayMaterial);

	UClass* PreviewStageAssetClass = LoadClass<AShipUpgradePreviewStage>(
		nullptr,
		TEXT("/Game/Blueprints/02_UI/UI_WorkTable/UI_ShipUpgrade/BP_ShipUpgradePreviewStage.BP_ShipUpgradePreviewStage_C"));
	TestNotNull(TEXT("The configured ship preview stage loads"), PreviewStageAssetClass);
	if (PreviewStageAssetClass)
	{
		UWorld* PreviewWorld = UWorld::CreateWorld(
			EWorldType::Game,
			false,
			TEXT("ShipUpgradePreviewStageTestWorld"));
		if (TestNotNull(TEXT("A preview test world can be created"), PreviewWorld))
		{
			AShipUpgradePreviewStage* FirstStage =
				PreviewWorld->SpawnActor<AShipUpgradePreviewStage>(PreviewStageAssetClass);
			AShipUpgradePreviewStage* SecondStage =
				PreviewWorld->SpawnActor<AShipUpgradePreviewStage>(PreviewStageAssetClass);
			TestNotNull(TEXT("The first preview stage spawns"), FirstStage);
			TestNotNull(TEXT("The second preview stage spawns"), SecondStage);
			if (FirstStage && SecondStage)
			{
				UTextureRenderTarget2D* FirstRenderTarget = FirstStage->GetRenderTarget();
				UTextureRenderTarget2D* SecondRenderTarget = SecondStage->GetRenderTarget();
				USceneCaptureComponent2D* Capture =
					FirstStage->FindComponentByClass<USceneCaptureComponent2D>();
				TestNotNull(TEXT("The preview stage owns a scene capture"), Capture);
				if (Capture)
				{
					TestFalse(
						TEXT("The preview uses explicit event-driven captures rather than CaptureEveryFrame"),
						Capture->bCaptureEveryFrame);
					TestEqual(
						TEXT("The preview capture exports inverse opacity for transparent UI composition"),
						Capture->CaptureSource,
						ESceneCaptureSource::SCS_SceneColorHDR);
				}
				TestNotNull(TEXT("The first stage owns a render target"), FirstRenderTarget);
				TestNotNull(TEXT("The second stage owns a render target"), SecondRenderTarget);
				TestNotEqual(
					TEXT("Concurrent upgrade screens never write into the same render target"),
					FirstRenderTarget,
					SecondRenderTarget);
				TestTrue(
					TEXT("Runtime render targets are transient rather than shared content assets"),
					FirstRenderTarget->HasAnyFlags(RF_Transient));
			}

			PreviewWorld->DestroyWorld(false);
		}
	}

	return true;
}

#endif
