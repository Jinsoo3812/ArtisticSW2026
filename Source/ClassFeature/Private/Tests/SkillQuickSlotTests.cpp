#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "BasePlayerState.h"
#include "BaseAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Cannon.h"
#include "Ship.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/InventoryComponent.h"
#include "Skills/PlayerSkillComponent.h"
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
		TEXT("GravityVortexLockOverlay"),
		TEXT("GravityVortexSelectedOverlay"),
		TEXT("WaterBombSlotPanel"),
		TEXT("WaterBombIconImage"),
		TEXT("WaterBombCooldownImage"),
		TEXT("WaterBombLockOverlay"),
		TEXT("WaterBombSelectedOverlay"),
		TEXT("BombardmentSlotPanel"),
		TEXT("BombardmentIconImage"),
		TEXT("BombardmentCooldownImage"),
		TEXT("BombardmentLockOverlay"),
		TEXT("BombardmentSelectedOverlay")
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

	const FName RemovedLegacyInputProperties[] =
	{
		TEXT("GravityVortexInputKey"),
		TEXT("WaterBombInputKey"),
		TEXT("BombardmentInputKey"),
		TEXT("ShuffleDuration"),
		TEXT("ShuffleOffset"),
		TEXT("ShufflePeakScale"),
		TEXT("ShufflePeakAngle")
	};
	for (const FName PropertyName : RemovedLegacyInputProperties)
	{
		TestNull(
			*FString::Printf(TEXT("Legacy key polling/shuffle property is removed: %s"), *PropertyName.ToString()),
			WidgetClass->FindPropertyByName(PropertyName));
	}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSkillQuickSlotAvailabilityOverlayTest,
	"ArtisticSW.UI.SkillQuickSlot.AvailabilityOverlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkillQuickSlotAvailabilityOverlayTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("QuestItem has an invalid ResultItemTag"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("QuestItem contains an invalid ingredient"), EAutomationExpectedErrorFlags::Contains, 2);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ABasePlayerState* State = World->SpawnActor<ABasePlayerState>();
	ABasePlayer* Player = World->SpawnActor<ABasePlayer>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	State->GetAbilitySystemComponent()->AddAttributeSetSubobject(State->GetAttributeSet());
	Player->SetPlayerState(State);
	Controller->Possess(Player);
	UClass* DesignerClass = LoadClass<USkillQuickSlotWidget>(nullptr,
		TEXT("/Game/Blueprints/02_UI/UI_HUD/UI_SkillQuickSlot/WBP_SkillQuickSlot.WBP_SkillQuickSlot_C"));
	USkillQuickSlotWidget* Widget = DesignerClass
		? NewObject<USkillQuickSlotWidget>(World, DesignerClass) : nullptr;
	if (!TestNotNull(TEXT("Skill quick-slot widget is created"), Widget))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}
	Widget->SetOwningPlayer(Controller);
	Widget->Initialize();
	Widget->InitializeForPlayer(Player);
	UBorder* Cover = Cast<UBorder>(Widget->WidgetTree->FindWidget(TEXT("GravityVortexLockOverlay")));
	if (TestNotNull(TEXT("Gravity Vortex cover exists"), Cover))
	{
		TestEqual(TEXT("Locked skill starts covered"), Cover->GetVisibility(), ESlateVisibility::HitTestInvisible);
		UInventoryComponent* Inventory = Player->GetInventoryComponent();
		Inventory->AddItem(Item_Id_Material_SkillMaterial_RareSkill, 1);
		TestEqual(TEXT("Material alone does not remove cover"), Cover->GetVisibility(), ESlateVisibility::HitTestInvisible);
		State->GetPlayerSkillComponent()->UnlockSkill(GameplayAbility_Skill_GravityVortex);
		TestEqual(TEXT("Available skill removes cover"), Cover->GetVisibility(), ESlateVisibility::Hidden);
		Inventory->RemoveItem(Item_Id_Material_SkillMaterial_RareSkill, 1);
		TestEqual(TEXT("Depleted material restores cover"), Cover->GetVisibility(), ESlateVisibility::HitTestInvisible);
		Player->bBypassSkillRequirementsForTesting = true;
		Widget->RefreshSlots();
		TestEqual(TEXT("Test bypass removes cover"), Cover->GetVisibility(), ESlateVisibility::Hidden);
	}
	UWidget* Selection = Widget->WidgetTree->FindWidget(TEXT("GravityVortexSelectedOverlay"));
	if (TestNotNull(TEXT("Gravity Vortex selection overlay exists"), Selection))
	{
		TestEqual(TEXT("Unselected skill has no selection overlay"),
			Selection->GetVisibility(), ESlateVisibility::Hidden);
		Player->OnGravityVortexSkillPressed();
		TestEqual(TEXT("Selecting the skill shows the selection overlay"),
			Selection->GetVisibility(), ESlateVisibility::HitTestInvisible);
		Player->OnGravityVortexSkillReleased();
		Player->OnGravityVortexSkillPressed();
		TestEqual(TEXT("Cancelling the skill hides the selection overlay"),
			Selection->GetVisibility(), ESlateVisibility::Hidden);
	}
	Widget->RemoveFromParent();
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSkillQuickSlotPawnModeTest,
	"ArtisticSW.UI.SkillQuickSlot.PawnModeFront",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkillQuickSlotPawnModeTest::RunTest(const FString& Parameters)
{
	// Existing shared crafting data is unrelated to the HUD mode transition.
	AddExpectedError(TEXT("QuestItem has an invalid ResultItemTag"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("QuestItem contains an invalid ingredient"), EAutomationExpectedErrorFlags::Contains, 2);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	USkillQuickSlotWidget* Widget = NewObject<USkillQuickSlotWidget>(World);
	UCanvasPanel* Canvas = NewObject<UCanvasPanel>(Widget);
	const FName Names[] = { TEXT("GravityVortexSlotPanel"), TEXT("WaterBombSlotPanel"), TEXT("BombardmentSlotPanel") };
	UCanvasPanelSlot* Slots[3];
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UOverlay* Panel = NewObject<UOverlay>(Widget);
		Slots[Index] = Canvas->AddChildToCanvas(Panel);
		FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Widget->GetClass(), Names[Index]);
		if (!TestNotNull(TEXT("Panel binding exists"), Property))
		{
			World->DestroyWorld(false);
			return false;
		}
		Property->SetObjectPropertyValue_InContainer(Widget, Panel);
	}
	APawn* Pawns[] = { World->SpawnActor<ABasePlayer>(), World->SpawnActor<ACannon>(), World->SpawnActor<AShip>() };
	const FGameplayTag Tags[] = { GameplayAbility_Skill_GravityVortex, GameplayAbility_Skill_WaterBomb, GameplayAbility_Skill_Bombardment };
	for (const int32 Index : { 0, 1, 2, 0 })
	{
		TestNotNull(TEXT("Mode pawn exists"), Pawns[Index]);
		Widget->RefreshEquippedState(Pawns[Index]);
		TestEqual(TEXT("Front skill follows the controlled pawn"), Widget->GetFrontSkillTag(), Tags[Index]);
		for (int32 Other = 0; Other < 3; ++Other)
		{
			if (Index != Other) TestTrue(TEXT("Active skill renders above both other panels"), Slots[Index]->GetZOrder() > Slots[Other]->GetZOrder());
		}
	}
	World->DestroyWorld(false);
	return true;
}

#endif
