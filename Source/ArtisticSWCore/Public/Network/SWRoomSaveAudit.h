#pragma once

#include "CoreMinimal.h"

class UStruct;
class UScriptStruct;

/** Opt-in event-only persistence journal (default OFF); independent of the load
 * performance profiler. Enable on the server with -SWRoomSaveAudit or console
 * sw.RoomSaveAudit 1. Disabling the journal does not disable readback validation. */
namespace SWRoomSaveAudit
{
ARTISTICSWCORE_API bool IsEnabled();
ARTISTICSWCORE_API void RecordStruct(const UScriptStruct* Type, const void* Data);
ARTISTICSWCORE_API void RecordObject(const TCHAR* Label, const UObject* Object);
ARTISTICSWCORE_API void RecordDifferences(const FString& Scope, const TArray<FString>& Differences);
ARTISTICSWCORE_API bool ComparePersistedObjects(const UObject* Expected, const UObject* Actual);

class ARTISTICSWCORE_API FScopedContext
{
public:
	FScopedContext(const TCHAR* Phase, const FGuid& RoomId, uint64 Sequence, const FString& Owner);
	~FScopedContext();
	FScopedContext(const FScopedContext&) = delete;
	FScopedContext& operator=(const FScopedContext&) = delete;
	FString Prefix;
private:
	FScopedContext* Previous = nullptr;
	bool bActive = false;
};
}
