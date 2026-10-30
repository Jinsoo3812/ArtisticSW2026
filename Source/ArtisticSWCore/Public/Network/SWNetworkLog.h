#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"

class IFileHandle;

ARTISTICSWCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogSWConnection, Log, All);
ARTISTICSWCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogSWRoom, Log, All);
ARTISTICSWCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogSWRoomSave, Log, All);

namespace SWRoomLogging
{
	/** Enabled by SWRoomDetailedLog or SWRoomLoadDiag; never in commandlets or Shipping. */
	ARTISTICSWCORE_API bool IsDetailedEnabled();
}

// Check before UE_LOG so suppressed messages do not format actor paths or state.
#if UE_BUILD_SHIPPING
#define SW_ROOM_DETAIL_LOG(Category, Verbosity, ...) do { } while (false)
#else
#define SW_ROOM_DETAIL_LOG(Category, Verbosity, ...) \
	do { if (SWRoomLogging::IsDetailedEnabled()) { UE_LOG(Category, Verbosity, __VA_ARGS__); } } while (false)
#endif

class ARTISTICSWCORE_API FSWConnectionFileOutputDevice final : public FOutputDevice
{
public:
	explicit FSWConnectionFileOutputDevice(bool bInRoomFlow = false, bool bInSaveTrace = false);
	virtual ~FSWConnectionFileOutputDevice() override;
	virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override;
	virtual void Flush() override;
	void FlushBuffered();
	bool IsOpen() const { return FileHandle != nullptr; }

private:
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
	friend class FSWRoomLogBufferTest;
	explicit FSWConnectionFileOutputDevice(TUniquePtr<IFileHandle> InFileHandle);
#endif
	void WarnOnce();
	bool DrainBuffer(bool bFullFlush);
	FCriticalSection Mutex;
	TUniquePtr<IFileHandle> FileHandle;
	TArray<uint8> PendingBytes;
	double LastDrainAt = 0.0;
	double PendingMutexSeconds = 0.0;
	double PendingFormatSeconds = 0.0;
	FString Side;
	bool bRoomFlow = false;
	bool bSaveTrace = false;
	bool bWarned = false;
};
