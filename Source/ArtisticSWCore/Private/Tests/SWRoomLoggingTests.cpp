#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Network/SWNetworkLog.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/AutomationTest.h"

namespace
{
struct FRoomLogTestState
{
	TArray<uint8> Bytes;
	int32 Writes = 0;
	int32 FullFlushes = 0;
	bool bFailWrite = false;
	bool bClosed = false;
};

class FRoomLogTestHandle final : public IFileHandle
{
public:
	explicit FRoomLogTestHandle(FRoomLogTestState& InState) : State(InState) {}
	virtual ~FRoomLogTestHandle() override { State.bClosed = true; }
	virtual int64 Tell() override { return State.Bytes.Num(); }
	virtual bool Seek(int64) override { return false; }
	virtual bool SeekFromEnd(int64) override { return false; }
	virtual bool Read(uint8*, int64) override { return false; }
	virtual bool ReadAt(uint8*, int64, int64) override { return false; }
	virtual bool Truncate(int64) override { return false; }
	virtual bool Write(const uint8* Source, int64 Count) override
	{
		if (State.bFailWrite) return false;
		State.Bytes.Append(Source, static_cast<int32>(Count));
		++State.Writes;
		return true;
	}
	virtual bool Flush(bool bFullFlush) override
	{
		State.FullFlushes += bFullFlush ? 1 : 0;
		return true;
	}
private:
	FRoomLogTestState& State;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWRoomLogBufferTest, "ArtisticSW.Network.RoomLogging.Buffer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWRoomLogBufferTest::RunTest(const FString& Parameters)
{
	FRoomLogTestState State;
	{
		TUniquePtr<IFileHandle> Handle = MakeUnique<FRoomLogTestHandle>(State);
		FSWConnectionFileOutputDevice Device(MoveTemp(Handle));
		Device.Serialize(TEXT("ignored"), ELogVerbosity::Display, LogTemp.GetCategoryName());
		Device.Serialize(TEXT("first"), ELogVerbosity::Display, LogSWConnection.GetCategoryName());
		Device.Serialize(TEXT("second"), ELogVerbosity::Display, LogSWConnection.GetCategoryName());
		TestEqual(TEXT("Ordinary lines remain buffered"), State.Writes, 0);
		Device.FlushBuffered();
		TestEqual(TEXT("One write drains both lines"), State.Writes, 1);
		TestEqual(TEXT("Periodic drain avoids full flush"), State.FullFlushes, 0);
		const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(State.Bytes.GetData()), State.Bytes.Num());
		const FString Written(Text.Length(), Text.Get());
		TestTrue(TEXT("Order is preserved"), Written.Find(TEXT("first")) < Written.Find(TEXT("second")));
		TestFalse(TEXT("Other categories are filtered"), Written.Contains(TEXT("ignored")));
		Device.Serialize(TEXT("third"), ELogVerbosity::Display, LogSWConnection.GetCategoryName());
		Device.Serialize(TEXT("failure-context"), ELogVerbosity::Error, LogSWConnection.GetCategoryName());
		TestEqual(TEXT("Errors drain pending context"), State.Writes, 2);
		TestEqual(TEXT("Errors force durable flush"), State.FullFlushes, 1);
		const FString LargeLine = FString::ChrN(70 * 1024, TEXT('x'));
		Device.Serialize(*LargeLine, ELogVerbosity::Display, LogSWConnection.GetCategoryName());
		TestEqual(TEXT("Size limit drains during blocked game ticks"), State.Writes, 3);
		Device.Serialize(TEXT("shutdown-tail"), ELogVerbosity::Display, LogSWConnection.GetCategoryName());
	}
	TestEqual(TEXT("Destructor drains final pending line"), State.Writes, 4);
	TestEqual(TEXT("Destructor performs full flush"), State.FullFlushes, 2);
	TestTrue(TEXT("Destructor closes the handle"), State.bClosed);
	FRoomLogTestState FailedState;
	{
		TUniquePtr<IFileHandle> Handle = MakeUnique<FRoomLogTestHandle>(FailedState);
		FSWConnectionFileOutputDevice Device(MoveTemp(Handle));
		Device.Serialize(TEXT("pending-before-failure"), ELogVerbosity::Display, LogSWConnection.GetCategoryName());
		FailedState.bFailWrite = true;
		AddExpectedMessagePlain(TEXT("SWRoom connection file log is unavailable"), ELogVerbosity::Warning);
		Device.FlushBuffered();
		TestFalse(TEXT("Write failure disables the sink"), Device.IsOpen());
		TestTrue(TEXT("Write failure closes the handle"), FailedState.bClosed);
		Device.Serialize(TEXT("after-failure"), ELogVerbosity::Error, LogSWConnection.GetCategoryName());
		Device.Flush();
	}
	int32 Evaluations = 0;
	SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display, TEXT("DetailTest=%d"), ++Evaluations);
	TestEqual(TEXT("Disabled detailed arguments are not evaluated"), Evaluations,
		SWRoomLogging::IsDetailedEnabled() ? 1 : 0);
	return true;
}

#endif
