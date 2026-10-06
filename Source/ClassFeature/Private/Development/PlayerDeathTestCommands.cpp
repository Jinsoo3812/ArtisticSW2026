#include "BasePlayerController.h"
#include "Development/TestInput/SWDevTestInputComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
namespace PlayerDeathTest
{
// Compatibility entry point only. The existing component owns the session
// command and server policy; never register a second global session variable.
void Suicide(const TArray<FString>& Args, UWorld* World)
{
	if (!Args.IsEmpty() || !World || !World->IsGameWorld()) return;
	ABasePlayerController* LocalController = nullptr;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		ABasePlayerController* Candidate = Cast<ABasePlayerController>(It->Get());
		if (!Candidate || !Candidate->IsLocalPlayerController()) continue;
		if (LocalController) return;
		LocalController = Candidate;
	}
	if (LocalController && LocalController->DevTestInput) LocalController->DevTestInput->RequestSuicide();
}

FAutoConsoleCommandWithWorldAndArgs SuicideCommand(
	TEXT("sw.DevTest.Suicide"),
	TEXT("Use the existing KillSelf action. Requires SW.DevTest.Session 1 and SW.DevTest.Input 1."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Suicide));
}
#endif
