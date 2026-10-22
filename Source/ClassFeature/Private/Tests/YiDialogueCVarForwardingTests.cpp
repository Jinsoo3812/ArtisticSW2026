#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR && !UE_BUILD_SHIPPING && !UE_BUILD_TEST

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "BasePlayerController.h"
#include "GameFramework/PlayerState.h"
#include "NPCDialogueData.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYiDialogueCVarServerApplyTest,
	"ArtisticSW.NPCDialogue.YiDialogueCVarServerApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FYiDialogueCVarServerApplyTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* Toggle = IConsoleManager::Get().FindConsoleVariable(TEXT("sw.Dialogue.YiSunSin.SkipRequirements"));
	if (!TestNotNull(TEXT("Dialogue CVar is registered"), Toggle)) return false;
	const int32 Previous = Toggle->GetInt();
	ON_SCOPE_EXIT { Toggle->Set(Previous, ECVF_SetByConsole); };
	Toggle->Set(0, ECVF_SetByConsole);
	ABasePlayerController* ServerController = NewObject<ABasePlayerController>();
	ServerController->ServerSetYiSunSinDialogueTestMode_Implementation(true);
	TestEqual(TEXT("An unauthenticated controller cannot change server test mode"), Toggle->GetInt(), 0);
	ServerController->PlayerState = NewObject<APlayerState>();
	ServerController->ServerSetYiSunSinDialogueTestMode_Implementation(true);
	TestEqual(TEXT("Server RPC applies the client enable request"), Toggle->GetInt(), 1);
	UNPCDialogueData* Data = LoadObject<UNPCDialogueData>(nullptr,
		TEXT("/Game/Campaign/DataAsset/Dialogue/DA_YiSunSinDialogue.DA_YiSunSinDialogue"));
	TestTrue(TEXT("Server dialogue observes the forwarded value"), Data && Data->IsYiSunSinTestMode());
	ServerController->ServerSetYiSunSinDialogueTestMode_Implementation(false);
	TestEqual(TEXT("Server RPC also applies the client disable request"), Toggle->GetInt(), 0);
	return true;
}

#endif
