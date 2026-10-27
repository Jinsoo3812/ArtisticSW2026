#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Network/SWConnectionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomReturnTimeoutTest,
	"ArtisticSW.Connection.RoomReturnTimeout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomReturnTimeoutTest::RunTest(const FString& Parameters)
{
	UGameInstance* Instance = NewObject<UGameInstance>();
	USWConnectionSubsystem* Connection = NewObject<USWConnectionSubsystem>(Instance);
	UClass* DriverClass = LoadClass<UNetDriver>(nullptr, TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"));
	if (!TestNotNull(TEXT("IP net driver class is available"), DriverClass)) return false;
	UNetDriver* Driver = NewObject<UNetDriver>(Instance, DriverClass);
	Driver->InitialConnectTimeout = 20.0f;
	Driver->ConnectionTimeout = 15.0f;
	const FFloatProperty* Resolution = FindFProperty<FFloatProperty>(DriverClass, TEXT("ResolutionConnectionTimeout"));
	if (!TestNotNull(TEXT("Resolution timeout property exists"), Resolution)) return false;
	Resolution->SetPropertyValue_InContainer(Driver, 20.0f);
	Connection->ApplyReturnReconnectTimeout(Driver);
	TestEqual(TEXT("Ordinary initial connection is unchanged"), Driver->InitialConnectTimeout, 20.0f);
	TestEqual(TEXT("Ordinary readiness remains 30 seconds"), Connection->GetActiveReadinessTimeout(), 30.0f);
	Connection->RoomLoadingReason = USWConnectionSubsystem::ERoomLoadingReason::Return;
	TestTrue(TEXT("Return ticks before map readiness starts"), Connection->IsTickable());
	Connection->ApplyReturnReconnectTimeout(Driver);
	Connection->ApplyReturnReconnectTimeout(Driver);
	TestEqual(TEXT("Return handshake waits 60 seconds"), Driver->InitialConnectTimeout, 60.0f);
	TestEqual(TEXT("Return connection waits 60 seconds"), Driver->ConnectionTimeout, 60.0f);
	TestEqual(TEXT("IP resolution connection waits 60 seconds"), Resolution->GetPropertyValue_InContainer(Driver), 60.0f);
	Resolution->SetPropertyValue_InContainer(Driver, 20.0f);
	Connection->ApplyReturnReconnectTimeout(Driver);
	TestEqual(TEXT("Return reasserts the resolution timeout"), Resolution->GetPropertyValue_InContainer(Driver), 60.0f);
	TestEqual(TEXT("Return readiness waits 60 seconds"), Connection->GetActiveReadinessTimeout(), 60.0f);
	Connection->CancelRoomReturnPresentation();
	TestEqual(TEXT("Original initial timeout restored"), Driver->InitialConnectTimeout, 20.0f);
	TestEqual(TEXT("Original connection timeout restored"), Driver->ConnectionTimeout, 15.0f);
	TestEqual(TEXT("Original resolution timeout restored"), Resolution->GetPropertyValue_InContainer(Driver), 20.0f);
	TestFalse(TEXT("Return-only ticking stops"), Connection->IsTickable());
	Connection->RoomLoadingReason = USWConnectionSubsystem::ERoomLoadingReason::Return;
	Connection->ApplyReturnReconnectTimeout(Driver);
	Connection->RestoreReturnReconnectTimeout();
	TestEqual(TEXT("Repeated return preserves original timeout"), Driver->InitialConnectTimeout, 20.0f);
	return true;
}

#endif
