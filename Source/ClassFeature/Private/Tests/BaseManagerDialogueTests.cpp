#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BaseManagerDialogueData.h"
#include "Engine/Blueprint.h"
#include "NPCCharacter.h"
#include "NPCDialogueSourceComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBaseManagerDialogueAssetTest,
	"ArtisticSW.NPCDialogue.BaseManagerAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBaseManagerDialogueAssetTest::RunTest(const FString& Parameters)
{
	const UBaseManagerDialogueData* Data = LoadObject<UBaseManagerDialogueData>(
		nullptr, TEXT("/Game/Blueprints/NPC/DA_BaseManagerDialogue.DA_BaseManagerDialogue"));
	if (!TestNotNull(TEXT("Base Manager dialogue DA loads as its dedicated class"), Data))
	{
		return false;
	}
	TestEqual(TEXT("NPC display name is authored exactly"), Data->DisplayName.ToString(), FString(TEXT("기지NPC")));

	const FNPCDialogueRule* Rule = Data->FindRule(TEXT("BaseManager_Default"));
	TestNotNull(TEXT("The always-available Base Manager rule exists"), Rule);
	if (Rule)
	{
		TestEqual(TEXT("Base Manager dialogue contains every menu and response line"), Rule->Lines.Num(), 14);
		const FNPCDialogueLine* Root = Rule->Lines.FindByPredicate([](const FNPCDialogueLine& Line)
		{
			return Line.LineId == TEXT("Root");
		});
		const FNPCDialogueLine* Menu = Rule->Lines.FindByPredicate([](const FNPCDialogueLine& Line)
		{
			return Line.LineId == TEXT("SkillMenu");
		});
		TestTrue(TEXT("Root offers skill unlock and exit"), Root && Root->Replies.Num() == 2);
		TestTrue(TEXT("Skill menu offers three skills and back"), Menu && Menu->Replies.Num() == 4);
		if (const FNPCDialogueLine* Success = Rule->Lines.FindByPredicate([](const FNPCDialogueLine& Line)
		{
			return Line.LineId == TEXT("CurrentSuccess1");
		}))
		{
			TestEqual(TEXT("Multi-line unlock dialogue advances explicitly"),
				Data->ResolveAdvanceTarget(*Rule, *Success), FName(TEXT("CurrentSuccess2")));
		}
	}

	const UBlueprint* Blueprint = LoadObject<UBlueprint>(
		nullptr, TEXT("/Game/Blueprints/NPC/BP_NPC_BaseManager.BP_NPC_BaseManager"));
	TestNotNull(TEXT("Base Manager NPC Blueprint exists"), Blueprint);
	const ANPCCharacter* NPC = Blueprint && Blueprint->GeneratedClass
		? Cast<ANPCCharacter>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	const UNPCDialogueSourceComponent* Source = NPC ? NPC->GetDialogueSourceComponent() : nullptr;
	TestTrue(TEXT("Base Manager Blueprint uses its dedicated dialogue DA"),
		Source && Source->GetDialogueData() == Data);
	return true;
}

#endif
