#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR && !UE_BUILD_SHIPPING && !UE_BUILD_TEST

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "BasePlayerController.h"
#include "GameFramework/PlayerState.h"
#include "NPCDialogueData.h"
#include "PlayerLifeTestWorld.h"
#include "Engine/NetDriver.h"

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
	PlayerLifeTests::FWorld Fixture;
	ABasePlayerController* ServerController = Fixture.World->SpawnActor<ABasePlayerController>();
	if (!TestNotNull(TEXT("Server controller exists"), ServerController)) return false;
	UClass* DriverClass = LoadClass<UNetDriver>(nullptr, TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"));
	if (!TestNotNull(TEXT("Dedicated test driver class exists"), DriverClass)) return false;
	UNetDriver* Driver = NewObject<UNetDriver>(Fixture.World.Get(), DriverClass);
	Driver->SetWorld(Fixture.World.Get());
	Fixture.World->SetNetDriver(Driver);
	GEngine->GetWorldContextFromWorldChecked(Fixture.World.Get()).RunAsDedicated = true;
	ON_SCOPE_EXIT
	{
		Fixture.World->SetNetDriver(nullptr);
		Driver->SetWorld(nullptr);
		GEngine->GetWorldContextFromWorldChecked(Fixture.World.Get()).RunAsDedicated = false;
	};
	TestTrue(TEXT("Authority fixture represents a dedicated server"), Fixture.World->GetNetMode() == NM_DedicatedServer);
	ServerController->ServerSetYiSunSinDialogueTestMode_Implementation(true);
	TestEqual(TEXT("An unauthenticated controller cannot change server test mode"), Toggle->GetInt(), 0);
	ServerController->PlayerState = Fixture.World->SpawnActor<APlayerState>();
	TestFalse(TEXT("Global campaign commands reject authenticated guests"), Fixture.Mode->CanChangeDevelopmentCampaignState(ServerController));
	ServerController->ServerSetYiSunSinDialogueTestMode_Implementation(true);
	TestEqual(TEXT("Authenticated guest cannot enable the server-wide bypass"), Toggle->GetInt(), 0);
	Fixture.Mode->HostControllers.Add(ServerController);
	TestTrue(TEXT("Global campaign commands accept the registered host"), Fixture.Mode->CanChangeDevelopmentCampaignState(ServerController));
	ServerController->ServerSetYiSunSinDialogueTestMode_Implementation(true);
	TestEqual(TEXT("Server RPC applies the client enable request"), Toggle->GetInt(), 1);
	UNPCDialogueData* Data = LoadObject<UNPCDialogueData>(nullptr,
		TEXT("/Game/Campaign/DataAsset/Dialogue/DA_YiSunSinDialogue.DA_YiSunSinDialogue"));
	TestTrue(TEXT("Server dialogue observes the forwarded value"), Data && Data->IsYiSunSinTestMode());
	Fixture.Mode->HostControllers.Remove(ServerController);
	ServerController->ServerSetYiSunSinDialogueTestMode_Implementation(false);
	TestEqual(TEXT("Guest cannot disable the host's server-wide setting either"), Toggle->GetInt(), 1);
	Fixture.Mode->HostControllers.Add(ServerController);
	ServerController->ServerSetYiSunSinDialogueTestMode_Implementation(false);
	TestEqual(TEXT("Server RPC also applies the client disable request"), Toggle->GetInt(), 0);
	return true;
}

#endif
