#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UI/EnemyHealthBarComponent.h"
#include "UI/EnemyHealthBarWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyHealthBarVisibilityStateTest,
	"ArtisticSW.Enemy.UI.HealthBar.VisibilityState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHealthBarVisibilityStateTest::RunTest(const FString& Parameters)
{
	const UEnemyHealthBarComponent* ComponentDefaults = GetDefault<UEnemyHealthBarComponent>();
	TestTrue(TEXT("Enemy health bar reveal state replicates"), ComponentDefaults->GetIsReplicated());
	TestTrue(
		TEXT("Enemy health bars use the dedicated enemy widget class"),
		ComponentDefaults->GetWidgetClass() == UEnemyHealthBarWidget::StaticClass());
	TestEqual(
		TEXT("A depleted health bar remains visible for 0.1 seconds by default"),
		ComponentDefaults->GetDeathHideDelay(),
		0.1f);

	TestFalse(
		TEXT("An enemy stays hidden before player damage"),
		UEnemyHealthBarComponent::ResolveShouldDisplay(false, true, true, true, false, true));

	TestTrue(
		TEXT("A revealed living enemy is visible while on-screen and unobstructed"),
		UEnemyHealthBarComponent::ResolveShouldDisplay(true, true, true, true, false, true));

	TestFalse(
		TEXT("Leaving the viewport hides without clearing the reveal latch"),
		UEnemyHealthBarComponent::ResolveShouldDisplay(true, false, true, true, false, true));
	TestTrue(
		TEXT("Returning to the viewport automatically shows the latched enemy again"),
		UEnemyHealthBarComponent::ResolveShouldDisplay(true, true, true, true, false, true));

	TestFalse(
		TEXT("LOS occlusion hides without clearing the reveal latch"),
		UEnemyHealthBarComponent::ResolveShouldDisplay(true, true, false, true, false, true));
	TestTrue(
		TEXT("Recovering LOS automatically shows the latched enemy again"),
		UEnemyHealthBarComponent::ResolveShouldDisplay(true, true, true, true, false, true));

	TestFalse(
		TEXT("Hiding the registered enemy mesh hides its health bar"),
		UEnemyHealthBarComponent::ResolveShouldDisplay(true, true, true, false, false, true));
	TestTrue(
		TEXT("Showing the registered enemy mesh automatically restores the latched health bar"),
		UEnemyHealthBarComponent::ResolveShouldDisplay(true, true, true, true, false, true));

	TestTrue(
		TEXT("The zero-health bar remains visible during the death hide delay"),
		UEnemyHealthBarComponent::ResolveShouldDisplay(true, true, true, true, false, true));
	TestFalse(
		TEXT("Death hides the health bar after the delay expires"),
		UEnemyHealthBarComponent::ResolveShouldDisplay(true, true, true, true, true, true));
	TestFalse(
		TEXT("A pooled inactive enemy always hides the health bar"),
		UEnemyHealthBarComponent::ResolveShouldDisplay(true, true, true, true, false, false));

	return true;
}

#endif
