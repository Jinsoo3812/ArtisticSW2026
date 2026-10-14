#include "Network/SWRoomSaveAudit.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/Crc.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Network/SWNetworkLog.h"
#include "GameFramework/Actor.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomStateAdapter.h"
#include "UObject/UnrealType.h"

namespace SWRoomSaveAudit
{
namespace
{
// Opt-in only: full bundle definition dumps can produce GB-sized journals.
// Enable on the SERVER process with -SWRoomSaveAudit for startup/disk-read evidence,
// or use `sw.RoomSaveAudit 1` in that process's console for subsequent events.
// `sw.RoomSaveAudit 0` disables recording; persistence/readback validation remains on.
// Keep the codec/capture/restore hooks for future investigations. See the usage note
// in Agent/Implementation_Spec/RoomSaveAudit_Usage_2026-10-12.md before enabling.
TAutoConsoleVariable<int32> AuditEnabled(TEXT("sw.RoomSaveAudit"), 0,
	TEXT("Detailed room persistence TSV journals. Default OFF. Server: -SWRoomSaveAudit or sw.RoomSaveAudit 1; 0 disables."));
thread_local FScopedContext* Context = nullptr;
FCriticalSection JournalMutex;
TUniquePtr<IFileHandle> Journal;
FString Pending;
bool bOpenAttempted = false;

FString Escape(FString Value)
{
	Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Value.ReplaceInline(TEXT("\t"), TEXT("\\t"));
	Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
	Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	return Value;
}

void Drain(bool bFlush)
{
	if (Journal && !Pending.IsEmpty())
	{
		FTCHARToUTF8 Encoded(*Pending);
		if (!Journal->Write(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length()))
		{
			Journal.Reset();
			UE_LOG(LogSWRoomSave, Error, TEXT("Flow=SaveAudit Result=Failed Reason=JournalWrite"));
		}
		Pending.Reset();
	}
	if (bFlush && Journal) Journal->Flush();
}

void Line(const FString& Type, const FString& Field, const TCHAR* Kind, const FString& Value)
{
	if (!Context) return;
	FScopeLock Lock(&JournalMutex);
	if (!bOpenAttempted)
	{
		bOpenAttempted = true;
		FString AbsoluteLog;
		const FString Directory = FParse::Value(FCommandLine::Get(), TEXT("abslog="), AbsoluteLog) && !FPaths::IsRelative(AbsoluteLog)
			? FPaths::GetPath(AbsoluteLog) : FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs/SWRoom"));
		const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("SaveAudit_%u.tsv"), FPlatformProcess::GetCurrentProcessId()));
		IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
		if (Files.CreateDirectoryTree(*Directory)) Journal.Reset(Files.OpenWrite(*Path, true, true));
		if (!Journal)
		{
			UE_LOG(LogSWRoomSave, Error, TEXT("Flow=SaveAudit Result=Failed Reason=JournalOpen Path=%s"), *Path);
		}
		else
		{
			Pending = TEXT("UTC\tPhase\tRoomId\tSequence\tOwner\tType\tField\tKind\tValue\n");
			UE_LOG(LogSWRoomSave, Display, TEXT("Flow=SaveAudit Result=Enabled Path=%s"), *Path);
		}
	}
	if (!Journal) return;
	Pending += FDateTime::UtcNow().ToIso8601() + TEXT("\t") + Context->Prefix + TEXT("\t")
		+ Escape(Type) + TEXT("\t") + Escape(Field) + TEXT("\t") + Kind + TEXT("\t") + Escape(Value) + TEXT("\n");
	if (Pending.Len() >= 128 * 1024) Drain(false);
}

bool HasSavedFields(const UStruct* Type)
{
	for (TFieldIterator<FProperty> It(Type); It; ++It)
		if (It->HasAnyPropertyFlags(CPF_SaveGame)) return true;
	return false;
}

void RecordValue(const FProperty* Property, const void* Value, const FString& Type, const FString& Path, const TCHAR* Kind);
void RecordFields(const UStruct* Type, const void* Data, const FString& Path, const TCHAR* Kind)
{
	for (TFieldIterator<FProperty> It(Type); It; ++It)
		if (It->HasAnyPropertyFlags(CPF_SaveGame))
			for (int32 Index = 0; Index < It->ArrayDim; ++Index)
				RecordValue(*It, It->ContainerPtrToValuePtr<void>(Data, Index), Type->GetPathName(),
					Path + It->GetName() + (It->ArrayDim > 1 ? FString::Printf(TEXT("[%d]"), Index) : TEXT("")), Kind);
}

void RecordValue(const FProperty* Property, const void* Value, const FString& Type, const FString& Path, const TCHAR* Kind)
{
	if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper Items(Array, Value);
		Line(Type, Path + TEXT(".Count"), Kind, LexToString(Items.Num()));
		if (CastField<FByteProperty>(Array->Inner))
		{
			Line(Type, Path + TEXT(".CRC32"), TEXT("OpaquePayload"), LexToString(FCrc::MemCrc32(Items.Num() ? Items.GetRawPtr(0) : nullptr, Items.Num())));
			return;
		}
		for (int32 Index = 0; Index < Items.Num(); ++Index)
			RecordValue(Array->Inner, Items.GetRawPtr(Index), Type, FString::Printf(TEXT("%s[%d]"), *Path, Index), Kind);
		return;
	}
	if (const FStructProperty* Struct = CastField<FStructProperty>(Property); Struct && HasSavedFields(Struct->Struct))
	{
		RecordFields(Struct->Struct, Value, Path + TEXT("."), Kind);
		return;
	}
	FString Text;
	Property->ExportTextItem_Direct(Text, Value, nullptr, nullptr, PPF_Delimited);
	Line(Type, Path, Kind, Text);
}

bool CompareValue(const FProperty* Property, const void* Before, const void* After, const FString& Type, const FString& Path)
{
	if (const FArrayProperty* Array = CastField<FArrayProperty>(Property); Array && !CastField<FByteProperty>(Array->Inner))
	{
		FScriptArrayHelper A(Array, Before), B(Array, After);
		bool bMatch = A.Num() == B.Num();
		if (!bMatch) Line(Type, Path + TEXT(".Count"), TEXT("Mismatch"), FString::Printf(TEXT("Expected=%d Actual=%d"), A.Num(), B.Num()));
		for (int32 Index = 0; Index < FMath::Min(A.Num(), B.Num()); ++Index)
			bMatch = CompareValue(Array->Inner, A.GetRawPtr(Index), B.GetRawPtr(Index), Type, FString::Printf(TEXT("%s[%d]"), *Path, Index)) && bMatch;
		return bMatch;
	}
	if (const FStructProperty* Struct = CastField<FStructProperty>(Property); Struct && HasSavedFields(Struct->Struct))
	{
		bool bMatch = true;
		for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
			if (It->HasAnyPropertyFlags(CPF_SaveGame))
				for (int32 Index = 0; Index < It->ArrayDim; ++Index)
					bMatch = CompareValue(*It, It->ContainerPtrToValuePtr<void>(Before, Index), It->ContainerPtrToValuePtr<void>(After, Index),
						Struct->Struct->GetPathName(), Path + TEXT(".") + It->GetName()
						+ (It->ArrayDim > 1 ? FString::Printf(TEXT("[%d]"), Index) : TEXT(""))) && bMatch;
		return bMatch;
	}
	if (Property->Identical(Before, After)) return true;
	Line(Type, Path, TEXT("Mismatch"), TEXT("Saved value differs from disk readback"));
	RecordValue(Property, Before, Type, Path, TEXT("MismatchExpected"));
	RecordValue(Property, After, Type, Path, TEXT("MismatchActual"));
	return false;
}
}

bool IsEnabled()
{
	if (IsRunningCommandlet()) return false;
	// Initialize once before the first record so startup reads are included. A later
	// console command takes precedence over this command-line setting, including OFF.
	static const bool bCommandLineConfigured = []()
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("SWRoomSaveAudit")))
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("sw.RoomSaveAudit")))
				Variable->Set(1, ECVF_SetByCommandline);
		return true;
	}();
	(void)bCommandLineConfigured;
	return AuditEnabled.GetValueOnAnyThread() != 0;
}

FScopedContext::FScopedContext(const TCHAR* Phase, const FGuid& RoomId, uint64 Sequence, const FString& Owner)
{
	if (!IsEnabled()) return;
	Previous = Context;
	Prefix = Escape(Phase) + TEXT("\t") + RoomId.ToString(EGuidFormats::Digits) + TEXT("\t") + LexToString(Sequence) + TEXT("\t") + Escape(Owner);
	bActive = true;
	Context = this;
	Line(TEXT("Event"), TEXT("Scope"), TEXT("Begin"), TEXT(""));
}

FScopedContext::~FScopedContext()
{
	if (!bActive) return;
	Line(TEXT("Event"), TEXT("Scope"), TEXT("End"), TEXT(""));
	Context = Previous;
	if (!Previous) { FScopeLock Lock(&JournalMutex); Drain(true); }
}

void RecordStruct(const UScriptStruct* Type, const void* Data)
{
	if (Context && Type && Data) RecordFields(Type, Data, Type->GetName() + TEXT("."), TEXT("SavedField"));
}

void RecordObject(const TCHAR* Label, const UObject* Object)
{
	if (!Context || !Object) return;
	if (const AActor* Actor = Cast<AActor>(Object))
	{
		const USWRoomSnapshotComponent* Identity = Actor->FindComponentByClass<USWRoomSnapshotComponent>();
		Line(Actor->GetClass()->GetPathName(), FString(Label) + TEXT(".Contract"), TEXT("Coverage"),
			FString::Printf(TEXT("SnapshotComponent=%d StableId=%s Persistence=%s DomainAdapter=%d Derived=%d PoolIdle=%d"),
				Identity != nullptr, Identity ? *Identity->StableId.ToString() : TEXT("None"),
				Identity ? *UEnum::GetValueAsString(Identity->PersistenceClass) : TEXT("Unregistered"),
				Cast<ISWRoomStateAdapter>(Actor) != nullptr, Actor->ActorHasTag(TEXT("RoomDerived")), Actor->ActorHasTag(TEXT("SWEnemyShipPoolIdle"))));
	}
	RecordFields(Object->GetClass(), Object, FString(Label) + TEXT("."), TEXT("SavedField"));
	// Replication alone does not persist a field; an adapter may own these values.
	for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		if (It->HasAnyPropertyFlags(CPF_Net) && !It->HasAnyPropertyFlags(CPF_SaveGame))
			for (int32 Index = 0; Index < It->ArrayDim; ++Index)
				RecordValue(*It, It->ContainerPtrToValuePtr<void>(Object, Index), Object->GetClass()->GetPathName(),
					FString(Label) + TEXT(".") + It->GetName()
					+ (It->ArrayDim > 1 ? FString::Printf(TEXT("[%d]"), Index) : TEXT("")), TEXT("ReplicatedOnlyRequiresAdapter"));
}

void RecordDifferences(const FString& Scope, const TArray<FString>& Differences)
{
	if (!Context) return;
	Line(TEXT("Comparison"), Scope, Differences.IsEmpty() ? TEXT("Match") : TEXT("Mismatch"), LexToString(Differences.Num()));
	for (const FString& Difference : Differences) Line(TEXT("Comparison"), Scope, TEXT("MismatchDetail"), Difference);
}

bool ComparePersistedObjects(const UObject* Expected, const UObject* Actual)
{
	if (!Expected || !Actual || Expected->GetClass() != Actual->GetClass()) return false;
	bool bMatch = true;
	for (TFieldIterator<FProperty> It(Expected->GetClass()); It; ++It)
		if (It->HasAnyPropertyFlags(CPF_SaveGame))
			for (int32 Index = 0; Index < It->ArrayDim; ++Index)
				bMatch = CompareValue(*It, It->ContainerPtrToValuePtr<void>(Expected, Index), It->ContainerPtrToValuePtr<void>(Actual, Index),
					Expected->GetClass()->GetPathName(), It->GetName()
					+ (It->ArrayDim > 1 ? FString::Printf(TEXT("[%d]"), Index) : TEXT(""))) && bMatch;
	Line(Expected->GetClass()->GetPathName(), TEXT("Readback"), bMatch ? TEXT("Match") : TEXT("Mismatch"), TEXT("All reflected SaveGame fields"));
	return bMatch;
}
}
