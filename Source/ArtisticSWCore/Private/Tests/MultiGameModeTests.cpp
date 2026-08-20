#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "MultiGameMode.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMultiGameModeUniformPawnTest,
	"ArtisticSW.GameMode.UniformPlayerSpawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMultiGameModeUniformPawnTest::RunTest(const FString& Parameters)
{
	AMultiGameMode* GameMode = NewObject<AMultiGameMode>();
	APlayerController* Controller0 = NewObject<APlayerController>();
	APlayerController* Controller1 = NewObject<APlayerController>();

	// 1. CommonPlayerPawnClass가 설정되었을 때 두 플레이어 모두 동일한 클래스를 반환받는지 검증
	GameMode->CommonPlayerPawnClass = APawn::StaticClass();
	
	UClass* PawnClass0 = GameMode->GetDefaultPawnClassForController(Controller0);
	UClass* PawnClass1 = GameMode->GetDefaultPawnClassForController(Controller1);

	TestNotNull(TEXT("Controller 0 receives valid pawn class"), PawnClass0);
	TestNotNull(TEXT("Controller 1 receives valid pawn class"), PawnClass1);
	TestEqual(TEXT("Both controllers receive identical CommonPlayerPawnClass"), PawnClass0, PawnClass1);
	TestEqual(TEXT("PawnClass matches CommonPlayerPawnClass"), PawnClass0, APawn::StaticClass());

	// 2. CommonPlayerPawnClass가 없고 DefaultPawnClass가 있을 때도 동일하게 반환되는지 검증
	GameMode->CommonPlayerPawnClass = nullptr;
	GameMode->DefaultPawnClass = APawn::StaticClass();

	UClass* FallbackClass0 = GameMode->GetDefaultPawnClassForController(Controller0);
	UClass* FallbackClass1 = GameMode->GetDefaultPawnClassForController(Controller1);

	TestEqual(TEXT("Both controllers receive identical DefaultPawnClass when CommonPlayerPawnClass is null"), FallbackClass0, FallbackClass1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS