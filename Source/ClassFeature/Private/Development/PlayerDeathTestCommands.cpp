#include "BasePlayerController.h"

#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BasePlayer.h"
#include "Components/BaseHealthComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlayerDeathTest, Log, All);

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
namespace PlayerDeathTest
{
	TAutoConsoleVariable<int32> SessionEnabled(
		TEXT("sw.DevTest.Session"), 0,
		TEXT("Enable development death tests on the server process: 0=off, 1=on. Not replicated."));

	const TCHAR* Describe(EDevelopmentDeathTestResult Result)
	{
		switch (Result)
		{
		case EDevelopmentDeathTestResult::Started: return TEXT("Started: existing death/respawn pipeline activated.");
		case EDevelopmentDeathTestResult::Disabled: return TEXT("Disabled: enable sw.DevTest.Session 1 on the SERVER.");
		case EDevelopmentDeathTestResult::NotAuthority: return TEXT("NotAuthority: death must execute on the server.");
		case EDevelopmentDeathTestResult::InvalidWorld: return TEXT("InvalidWorld: run in a playing game world.");
		case EDevelopmentDeathTestResult::InvalidPawn: return TEXT("InvalidPawn: no owned player character, or the requested pawn was replaced.");
		case EDevelopmentDeathTestResult::AlreadyDead: return TEXT("AlreadyDead: death has already started.");
		default: return TEXT("NotReady: health/ability system is not ready for this pawn.");
		}
	}

	void Suicide(const TArray<FString>& Args, UWorld* World)
	{
		if (!Args.IsEmpty() || !World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
		{
			UE_LOG(LogPlayerDeathTest, Display,
				TEXT("[DevDeath] Use sw.DevTest.Suicide (no arguments) in the intended player's PIE/game console."));
			return;
		}

		// Resolve within the console's world, never through GWorld or another PIE instance.
		ABasePlayerController* LocalController = nullptr;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			ABasePlayerController* Candidate = Cast<ABasePlayerController>(It->Get());
			if (!Candidate || !Candidate->IsLocalController()) continue;
			if (LocalController)
			{
				UE_LOG(LogPlayerDeathTest, Display, TEXT("[DevDeath] Multiple local players: this console command requires one local player per world."));
				return;
			}
			LocalController = Candidate;
		}
		if (LocalController) LocalController->RequestDevelopmentSuicide();
		else UE_LOG(LogPlayerDeathTest, Display, TEXT("[DevDeath] No local BasePlayerController in console world %s."), *World->GetName());
	}

	FAutoConsoleCommandWithWorldAndArgs SuicideCommand(
		TEXT("sw.DevTest.Suicide"),
		TEXT("Request death for this window's player. Server must enable sw.DevTest.Session 1. No arguments."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Suicide));
}
#endif

void ABasePlayerController::RequestDevelopmentSuicide()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (!IsLocalController()) return;
	APawn* RequestedPawn = GetPawn();
	if (!IsValid(RequestedPawn))
	{
		ClientDevelopmentDeathTestResult_Implementation(EDevelopmentDeathTestResult::InvalidPawn);
		return;
	}
	UE_LOG(LogPlayerDeathTest, Display, TEXT("[DevDeath] Request Controller=%s Pawn=%s World=%s NetMode=%d"),
		*GetName(), *RequestedPawn->GetName(), *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()));
	ServerRequestDevelopmentSuicide(RequestedPawn);
#endif
}

EDevelopmentDeathTestResult ABasePlayerController::TryStartDevelopmentDeath(APawn* ExpectedPawn)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (!HasAuthority()) return EDevelopmentDeathTestResult::NotAuthority;
	if (PlayerDeathTest::SessionEnabled.GetValueOnGameThread() != 1) return EDevelopmentDeathTestResult::Disabled;
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || World->bIsTearingDown || IsActorBeingDestroyed())
		return EDevelopmentDeathTestResult::InvalidWorld;

	ABasePlayer* RequestedPlayer = Cast<ABasePlayer>(ExpectedPawn);
	if (!IsValid(RequestedPlayer) || RequestedPlayer != GetPawn() || RequestedPlayer->GetController() != this
		|| RequestedPlayer->GetWorld() != World || RequestedPlayer->IsActorBeingDestroyed())
		return EDevelopmentDeathTestResult::InvalidPawn;
	UBaseHealthComponent* Health = RequestedPlayer->GetHealthComponent();
	UAbilitySystemComponent* ASC = RequestedPlayer->GetAbilitySystemComponent();
	if (!Health || !ASC || ASC->GetAvatarActor() != RequestedPlayer || !ASC->GetSet<UBaseAttributeSet>())
		return EDevelopmentDeathTestResult::NotReady;
	if (Health->IsDead()) return EDevelopmentDeathTestResult::AlreadyDead;

	// No damage source or artificial hit impulse: use the existing health/death pipeline.
	// Health changes normally start death synchronously. The fallback handles an
	// already-zero health value or authored modifiers that prevent that notification.
	ASC->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), 0.0f);
	if (!Health->IsDead()) Health->StartDeath();
	RequestedPlayer->ForceNetUpdate();
	return Health->IsDead() ? EDevelopmentDeathTestResult::Started : EDevelopmentDeathTestResult::NotReady;
#else
	return EDevelopmentDeathTestResult::Disabled;
#endif
}

void ABasePlayerController::ServerRequestDevelopmentSuicide_Implementation(APawn* ExpectedPawn)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	const EDevelopmentDeathTestResult Result = TryStartDevelopmentDeath(ExpectedPawn);
	UE_LOG(LogPlayerDeathTest, Display, TEXT("[DevDeath] Server Controller=%s RequestedPawn=%s Result=%s"),
		*GetName(), *GetNameSafe(ExpectedPawn), PlayerDeathTest::Describe(Result));
	ClientDevelopmentDeathTestResult(Result);
#endif
}

void ABasePlayerController::ClientDevelopmentDeathTestResult_Implementation(EDevelopmentDeathTestResult Result)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	const FString Message = FString::Printf(TEXT("[DevDeath] %s"), PlayerDeathTest::Describe(Result));
	UE_LOG(LogPlayerDeathTest, Display, TEXT("%s"), *Message);
	ClientMessage(Message);
#endif
}
