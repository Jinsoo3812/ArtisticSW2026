#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Room/SWRoomSnapshotTypes.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "SWRoomStateAdapter.generated.h"

class AActor;

UINTERFACE()
class ARTISTICSWCORE_API USWRoomStateAdapter : public UInterface
{
	GENERATED_BODY()
};

class ARTISTICSWCORE_API ISWRoomStateAdapter
{
	GENERATED_BODY()
public:
	virtual void CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const = 0;
	virtual bool RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError) = 0;
	virtual bool CompareRoomDomain(const FSWRoomDomainPart& Expected, const FSWRoomDomainPart& Actual,
		float TimeToleranceSeconds, TArray<FString>& OutFields) const
	{
		if (Expected.Domain != Actual.Domain)
			OutFields.Add(FString::Printf(TEXT("Field=Domain Expected=%d Actual=%d"), static_cast<int32>(Expected.Domain), static_cast<int32>(Actual.Domain)));
		if (Expected.Version != Actual.Version)
			OutFields.Add(FString::Printf(TEXT("Field=Version Expected=%d Actual=%d"), Expected.Version, Actual.Version));
		if (Expected.Bytes != Actual.Bytes)
			OutFields.Add(FString::Printf(TEXT("Field=Bytes Expected=%d bytes Actual=%d bytes"), Expected.Bytes.Num(), Actual.Bytes.Num()));
		return OutFields.IsEmpty();
	}
	virtual bool FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError) { return true; }
};

struct ARTISTICSWCORE_API FSWRoomStructCodec
{
	static bool CompareSaveGameStruct(const UScriptStruct* Type, const void* Expected, const void* Actual,
		float TimeToleranceSeconds, TArray<FString>& OutFields);

	template <typename T>
	static bool Compare(const FSWRoomDomainPart& Expected, const FSWRoomDomainPart& Actual,
		float TimeToleranceSeconds, TArray<FString>& OutFields)
	{
		if (Expected.Domain != Actual.Domain)
			OutFields.Add(FString::Printf(TEXT("Field=Domain Expected=%d Actual=%d"), static_cast<int32>(Expected.Domain), static_cast<int32>(Actual.Domain)));
		if (Expected.Version != Actual.Version)
			OutFields.Add(FString::Printf(TEXT("Field=Version Expected=%d Actual=%d"), Expected.Version, Actual.Version));
		T Before, After;
		if (!Read(Expected.Bytes, Before) || !Read(Actual.Bytes, After))
		{
			OutFields.Add(TEXT("Field=Payload Expected=Readable Actual=Invalid"));
			return false;
		}
		CompareSaveGameStruct(T::StaticStruct(), &Before, &After, TimeToleranceSeconds, OutFields);
		return OutFields.IsEmpty();
	}
	template <typename T>
	static bool Write(const T& Value, TArray<uint8>& OutBytes)
	{
		OutBytes.Reset();
		FMemoryWriter Writer(OutBytes, true);
		FObjectAndNameAsStringProxyArchive Archive(Writer, false);
		Archive.ArIsSaveGame = true;
		T::StaticStruct()->SerializeItem(Archive, const_cast<T*>(&Value), nullptr);
		return !Archive.IsError();
	}

	template <typename T>
	static bool Read(const TArray<uint8>& Bytes, T& OutValue)
	{
		if (Bytes.IsEmpty()) return false;
		FMemoryReader Reader(Bytes, true);
		FObjectAndNameAsStringProxyArchive Archive(Reader, true);
		Archive.ArIsSaveGame = true;
		T::StaticStruct()->SerializeItem(Archive, &OutValue, nullptr);
		return !Archive.IsError() && Reader.Tell() == Bytes.Num();
	}
};
