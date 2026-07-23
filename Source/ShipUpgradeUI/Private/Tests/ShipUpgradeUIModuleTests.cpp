#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
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

	return true;
}

#endif
