#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "UI/SkillQuickSlotWidget.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSkillQuickSlotWidgetContractTest,
	"ArtisticSW.UI.SkillQuickSlot.WidgetContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkillQuickSlotWidgetContractTest::RunTest(const FString& Parameters)
{
	const UClass* WidgetClass = USkillQuickSlotWidget::StaticClass();
	TestNotNull(TEXT("Skill quick slot native class exists"), WidgetClass);

	const FName RequiredWidgetProperties[] =
	{
		TEXT("GravityVortexSlotPanel"),
		TEXT("GravityVortexIconImage"),
		TEXT("GravityVortexCooldownImage"),
		TEXT("GravityVortexInputKeyText"),
		TEXT("GravityVortexLockOverlay"),
		TEXT("WaterBombSlotPanel"),
		TEXT("WaterBombIconImage"),
		TEXT("WaterBombCooldownImage"),
		TEXT("WaterBombInputKeyText"),
		TEXT("WaterBombLockOverlay"),
		TEXT("BombardmentSlotPanel"),
		TEXT("BombardmentIconImage"),
		TEXT("BombardmentCooldownImage"),
		TEXT("BombardmentInputKeyText"),
		TEXT("BombardmentLockOverlay")
	};

	for (const FName PropertyName : RequiredWidgetProperties)
	{
		TestNotNull(
			*FString::Printf(TEXT("Designer binding exists: %s"), *PropertyName.ToString()),
			WidgetClass->FindPropertyByName(PropertyName));
	}

	TestNotNull(
		TEXT("Blueprint cooldown display API exists"),
		WidgetClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(USkillQuickSlotWidget, SetSkillCooldown)));
	TestNotNull(
		TEXT("Blueprint front-skill query exists"),
		WidgetClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(USkillQuickSlotWidget, GetFrontSkillTag)));

	UClass* DesignerClass = LoadClass<USkillQuickSlotWidget>(
		nullptr,
		TEXT("/Game/Blueprints/02_UI/UI_HUD/UI_SkillQuickSlot/WBP_SkillQuickSlot.WBP_SkillQuickSlot_C"));
	if (!TestNotNull(TEXT("WBP_SkillQuickSlot generated class is loadable"), DesignerClass))
	{
		return false;
	}

	const UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(DesignerClass);
	const UWidgetTree* Tree = GeneratedClass ? GeneratedClass->GetWidgetTreeArchetype() : nullptr;
	if (!TestNotNull(TEXT("WBP_SkillQuickSlot has a designer widget tree"), Tree))
	{
		return false;
	}

	for (const FName PropertyName : RequiredWidgetProperties)
	{
		TestNotNull(
			*FString::Printf(TEXT("Required designer variable exists: %s"), *PropertyName.ToString()),
			Tree->FindWidget(PropertyName));
	}

	const FName SlotPanelNames[] =
	{
		TEXT("GravityVortexSlotPanel"),
		TEXT("WaterBombSlotPanel"),
		TEXT("BombardmentSlotPanel")
	};
	for (const FName SlotPanelName : SlotPanelNames)
	{
		const UWidget* SlotPanel = Tree->FindWidget(SlotPanelName);
		TestNotNull(*FString::Printf(TEXT("%s exists"), *SlotPanelName.ToString()), SlotPanel);
		TestNotNull(
			*FString::Printf(TEXT("%s is a direct SkillSlotCanvas child"), *SlotPanelName.ToString()),
			SlotPanel ? Cast<UCanvasPanelSlot>(SlotPanel->Slot) : nullptr);
	}

	return true;
}

#endif
