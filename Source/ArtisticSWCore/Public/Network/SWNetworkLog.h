#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"

class IFileHandle;

ARTISTICSWCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogSWConnection, Log, All);

class ARTISTICSWCORE_API FSWConnectionFileOutputDevice final : public FOutputDevice
{
public:
	FSWConnectionFileOutputDevice();
	virtual ~FSWConnectionFileOutputDevice() override;
	virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override;
	virtual void Flush() override;
	bool IsOpen() const { return FileHandle != nullptr; }

private:
	void WarnOnce();
	FCriticalSection Mutex;
	TUniquePtr<IFileHandle> FileHandle;
	FString Side;
	bool bWarned = false;
};
