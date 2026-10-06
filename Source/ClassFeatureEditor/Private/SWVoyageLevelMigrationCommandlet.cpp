#include "SWVoyageLevelMigrationCommandlet.h"
#include "Room/SWVoyageResetAnchor.h"
#include "Room/SWVoyageResetProfile.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "EditorLevelUtils.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/Brush.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Components/ChildActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "FileHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "UObject/SoftObjectPath.h"
#include "Interfaces/IProjectManager.h"
#include "ProjectDescriptor.h"
#include "ModuleDescriptor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Editor/UnrealEdEngine.h"
#include "Selection.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Interfaces/TypedElementSelectionInterface.h"
#include "Elements/Interfaces/TypedElementWorldInterface.h"
#include "Elements/Actor/ActorElementEditorSelectionInterface.h"
#include "Elements/Actor/ActorElementEditorWorldInterface.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "Misc/ConfigCacheIni.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <bcrypt.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
bool bEngineConfigOverridden = false;
bool bEngineConfigKeyExisted = false;
FString PreviousEditorEngine;

struct FScopedEngineConfigRestore
{
	~FScopedEngineConfigRestore()
	{
		if (!bEngineConfigOverridden || !GConfig) return;
		if (bEngineConfigKeyExisted) GConfig->SetString(TEXT("/Script/Engine.Engine"), TEXT("EditorEngine"), *PreviousEditorEngine, GEngineIni);
		else GConfig->RemoveKey(TEXT("/Script/Engine.Engine"), TEXT("EditorEngine"), GEngineIni);
		bEngineConfigOverridden = false; PreviousEditorEngine.Reset();
	}
};

struct FScopedVoyageSelection
{
	UTypedElementRegistry* Registry = nullptr;
	TStrongObjectPtr<UObject> OriginalActorSelectionInterface;
	TStrongObjectPtr<UObject> OriginalActorWorldInterface;
	TStrongObjectPtr<UTypedElementSelectionSet> OriginalActors;
	TStrongObjectPtr<UTypedElementSelectionSet> OriginalComponents;
	TStrongObjectPtr<UTypedElementSelectionSet> Selection;
	bool bInitialized = false;
	bool Initialize(FString& Error)
	{
		Registry = UTypedElementRegistry::GetInstance();
		if (!Registry || !GEditor || !GEditor->GetSelectedActors() || !GEditor->GetSelectedComponents()) { Error = TEXT("SelectionRegistryMissing"); return false; }
		const FTypedHandleTypeId ActorType = Registry->GetRegisteredElementTypeId(NAME_Actor);
		ITypedElementSelectionInterface* Select = Registry->GetElementInterface<ITypedElementSelectionInterface>(ActorType);
		ITypedElementWorldInterface* World = Registry->GetElementInterface<ITypedElementWorldInterface>(ActorType);
		if (!ActorType || !Select || !World) { Error = TEXT("ActorElementInterfacesMissing"); return false; }
		OriginalActorSelectionInterface.Reset(Select->_getUObject()); OriginalActorWorldInterface.Reset(World->_getUObject());
		OriginalActors.Reset(GEditor->GetSelectedActors()->GetElementSelectionSet()); OriginalComponents.Reset(GEditor->GetSelectedComponents()->GetElementSelectionSet());
		Registry->RegisterElementInterface<ITypedElementSelectionInterface>(NAME_Actor, NewObject<UActorElementEditorSelectionInterface>(), true);
		Registry->RegisterElementInterface<ITypedElementWorldInterface>(NAME_Actor, NewObject<UActorElementEditorWorldInterface>(), true);
		Selection.Reset(NewObject<UTypedElementSelectionSet>());
		GEditor->GetSelectedActors()->SetElementSelectionSet(Selection.Get()); GEditor->GetSelectedComponents()->SetElementSelectionSet(Selection.Get());
		bInitialized = true; return true;
	}
	~FScopedVoyageSelection()
	{
		if (!bInitialized) return;
		Selection->ClearSelection(FTypedElementSelectionOptions()); Selection->NotifyPendingChanges();
		GEditor->GetSelectedActors()->SetElementSelectionSet(OriginalActors.Get()); GEditor->GetSelectedComponents()->SetElementSelectionSet(OriginalComponents.Get());
		Registry->RegisterElementInterface<ITypedElementSelectionInterface>(NAME_Actor, OriginalActorSelectionInterface.Get(), true);
		Registry->RegisterElementInterface<ITypedElementWorldInterface>(NAME_Actor, OriginalActorWorldInterface.Get(), true);
	}
};

struct FMigrationRow
{
	FString Path, Class, Tags, Location, Name;
};

struct FActorBefore
{
	FTransform Transform;
	FString Class;
	FString ParentKey;
	TArray<FName> Tags;
	FGuid StableId;
	TMap<FString, FString> Properties;
	TMap<FString, FString> ComponentClasses;
	TMap<FString, ECollisionEnabled::Type> ComponentCollisionModes;
	TMap<FString, TSharedPtr<FBodyInstance>> ComponentCollisionProperties;
	ESpawnActorCollisionHandlingMethod SpawnCollisionHandling = ESpawnActorCollisionHandlingMethod::Undefined;
};

struct FSoftReferenceState
{
	FString Path;
	FString WorldTargetKey;
	bool bResolved = false;
	bool bWorldActorTarget = false;
};

using FPathAliases = TArray<TPair<FString, FString>>;
using FLogicalActorKeys = TMap<TWeakObjectPtr<AActor>, FString>;
using FSoftReferenceMap = TMap<FString, TArray<FSoftReferenceState>>;

bool FileHash(const FString& Filename, FString& OutHex, FString& OutError)
{
	OutHex.Reset(); OutError.Reset();
#if PLATFORM_WINDOWS
	TArray<uint8> Data;
	if (!FFileHelper::LoadFileToArray(Data, *Filename)) { OutError = TEXT("SHA256ReadFailed:") + Filename; return false; }
	BCRYPT_ALG_HANDLE Algorithm = nullptr;
	BCRYPT_HASH_HANDLE Hash = nullptr;
	const auto Failed = [&](NTSTATUS Status, const TCHAR* Operation)
	{
		OutError = FString::Printf(TEXT("SHA256Failed File=%s Operation=%s NTSTATUS=%08x"), *Filename, Operation, static_cast<uint32>(Status));
		if (Hash) BCryptDestroyHash(Hash);
		if (Algorithm) BCryptCloseAlgorithmProvider(Algorithm, 0);
		return false;
	};
	NTSTATUS Status = BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
	if (Status < 0) return Failed(Status, TEXT("OpenAlgorithmProvider"));
	ULONG ObjectLength = 0, HashLength = 0, ResultLength = 0;
	Status = BCryptGetProperty(Algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&ObjectLength), sizeof(ObjectLength), &ResultLength, 0);
	if (Status < 0 || ResultLength != sizeof(ObjectLength) || ObjectLength > MAX_int32) return Failed(Status, TEXT("GetObjectLength"));
	Status = BCryptGetProperty(Algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&HashLength), sizeof(HashLength), &ResultLength, 0);
	if (Status < 0 || ResultLength != sizeof(HashLength) || HashLength != 32) return Failed(Status, TEXT("GetHashLength"));
	TArray<uint8> Object; Object.SetNumUninitialized(static_cast<int32>(ObjectLength));
	Status = BCryptCreateHash(Algorithm, &Hash, Object.GetData(), ObjectLength, nullptr, 0, 0);
	if (Status < 0) return Failed(Status, TEXT("CreateHash"));
	for (uint64 Offset = 0; Offset < static_cast<uint64>(Data.Num());)
	{
		const ULONG Chunk = static_cast<ULONG>(FMath::Min<uint64>(static_cast<uint64>(Data.Num()) - Offset, MAX_uint32));
		Status = BCryptHashData(Hash, Data.GetData() + Offset, Chunk, 0);
		if (Status < 0) return Failed(Status, TEXT("HashData"));
		Offset += Chunk;
	}
	uint8 Digest[32]; Status = BCryptFinishHash(Hash, Digest, sizeof(Digest), 0);
	if (Status < 0) return Failed(Status, TEXT("FinishHash"));
	Status = BCryptDestroyHash(Hash); Hash = nullptr;
	if (Status < 0) return Failed(Status, TEXT("DestroyHash"));
	Status = BCryptCloseAlgorithmProvider(Algorithm, 0); Algorithm = nullptr;
	if (Status < 0) return Failed(Status, TEXT("CloseAlgorithmProvider"));
	OutHex = BytesToHex(Digest, sizeof(Digest)); return true;
#else
	OutError = TEXT("SHA256UnsupportedPlatform:") + Filename; return false;
#endif
}

bool IsHelper(const AActor* Actor)
{
	return !IsValid(Actor) || Actor->IsA<AWorldSettings>() || Actor->IsA<ABrush>() || Actor->IsEditorOnly();
}

bool IsVoyageLifetimeTag(const FString& Value)
{
	static const TCHAR* LifetimeTags[] = {
		TEXT("SWVoyage.Environment"), TEXT("SWVoyage.Anchor"), TEXT("SWVoyage.Voyage"),
		TEXT("SWVoyage.SharedService"), TEXT("SWVoyage.PlayerLife"), TEXT("SWVoyage.LocalPresentation")
	};
	for (const TCHAR* Tag : LifetimeTags) if (Value == Tag) return true;
	return false;
}

FString TagsToString(const TArray<FName>& Tags)
{
	FString Result;
	for (FName Tag : Tags) Result += Tag.ToString() + TEXT(";");
	return Result;
}

FString ManifestTags(const AActor* Actor, bool bMigrated)
{
	FString Result;
	for (FName Tag : Actor->Tags)
	{
		if (bMigrated && IsVoyageLifetimeTag(Tag.ToString())) continue;
		Result += Tag.ToString() + TEXT(";");
	}
	return Result;
}

bool TagsMatch(const AActor* Actor, const FString& Expected, bool bMigrated)
{
	if (!bMigrated) return ManifestTags(Actor, false) == Expected;
	const USWRoomSnapshotComponent* Snapshot = Actor->FindComponentByClass<USWRoomSnapshotComponent>();
	int32 IdCount = 0;
	int32 ExpectedIdCount = 0;
	FGuid ActualId, ExpectedId;
	FString WithoutId;
	FString ExpectedWithoutId;
	for (FName Tag : Actor->Tags)
	{
		const FString Value = Tag.ToString();
		if (IsVoyageLifetimeTag(Value)) continue;
		if (Value.StartsWith(TEXT("SWRoomStableId=")))
		{
			if (!Snapshot || !FGuid::Parse(Value.Mid(15), ActualId) || !ActualId.IsValid() || ActualId != Snapshot->StableId) return false;
			++IdCount;
		}
		else WithoutId += Value + TEXT(";");
	}
	TArray<FString> ExpectedTags;
	// TagsToString terminates each tag with ';'. Empty delimiter fields
	// represent no tag, including a manifest row whose tag column is empty.
	Expected.ParseIntoArray(ExpectedTags, TEXT(";"), true);
	for (const FString& Value : ExpectedTags)
	{
		if (Value.StartsWith(TEXT("SWRoomStableId=")))
		{
			if (++ExpectedIdCount > 1 || !FGuid::Parse(Value.Mid(15), ExpectedId) || !ExpectedId.IsValid()) return false;
			continue;
		}
		ExpectedWithoutId += Value + TEXT(";");
	}
	return WithoutId == ExpectedWithoutId && ExpectedIdCount <= 1 && IdCount == (Snapshot && Snapshot->StableId.IsValid() ? 1 : 0)
		&& (ExpectedIdCount == 0 || ActualId == ExpectedId);
}

bool GetManifestStableId(const FMigrationRow& Row, FGuid& OutId)
{
	OutId.Invalidate();
	TArray<FString> Tags;
	Row.Tags.ParseIntoArray(Tags, TEXT(";"), false);
	for (const FString& Tag : Tags)
		if (Tag.StartsWith(TEXT("SWRoomStableId=")))
			return FGuid::Parse(Tag.Mid(15), OutId) && OutId.IsValid();
	return false;
}

bool TagsMatchAfterMigration(const TArray<FName>& BeforeTags, const TArray<FName>& AfterTags, bool bMayAssignStableId)
{
	TArray<FName> Expected, Actual;
	FGuid BeforeStableId, AfterStableId;
	int32 BeforeIdCount = 0, AfterIdCount = 0;
	for (FName Tag : BeforeTags)
	{
		const FString Value = Tag.ToString();
		if (IsVoyageLifetimeTag(Value)) continue;
		if (Value.StartsWith(TEXT("SWRoomStableId=")))
		{
			if (++BeforeIdCount != 1 || !FGuid::Parse(Value.Mid(15), BeforeStableId) || !BeforeStableId.IsValid()) return false;
			continue;
		}
		Expected.Add(Tag);
	}
	for (FName Tag : AfterTags)
	{
		const FString Value = Tag.ToString();
		if (IsVoyageLifetimeTag(Value)) continue;
		if (Value.StartsWith(TEXT("SWRoomStableId=")))
		{
			if (++AfterIdCount != 1 || !FGuid::Parse(Value.Mid(15), AfterStableId) || !AfterStableId.IsValid()) return false;
			continue;
		}
		Actual.Add(Tag);
	}
	Expected.Sort(FNameLexicalLess()); Actual.Sort(FNameLexicalLess());
	if (Expected != Actual || AfterIdCount > 1 || (BeforeIdCount > 0 && BeforeStableId != AfterStableId)) return false;
	return BeforeIdCount > 0 || bMayAssignStableId || AfterIdCount == 0;
}

bool ValidateIds(UWorld* World, bool bRequireIds, FString& Error)
{
	TSet<FGuid> Used;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		USWRoomSnapshotComponent* Snapshot = It->FindComponentByClass<USWRoomSnapshotComponent>();
		if (!Snapshot || Snapshot->PersistenceClass == ESWRoomPersistenceClass::Transient) continue;
		const FGuid SerializedId = Snapshot->StableId;
		const bool bResolved = Snapshot->RefreshLevelInstanceId();
		if (!bResolved)
		{
			if (bRequireIds || It->GetParentActor()) { Error = TEXT("StableIdMissing:") + It->GetPathName(); return false; }
			continue;
		}
		if (bRequireIds && SerializedId != Snapshot->StableId)
		{
			Error = FString::Printf(TEXT("SerializedIdMismatch:%s Serialized=%s Derived=%s WasLoaded=%d Parent=%s Tags=%s"),
				*It->GetPathName(), *SerializedId.ToString(EGuidFormats::Digits), *Snapshot->StableId.ToString(EGuidFormats::Digits),
				It->HasAnyFlags(RF_WasLoaded), *GetPathNameSafe(It->GetParentActor()), *TagsToString(It->Tags));
			return false;
		}
		if (Used.Contains(Snapshot->StableId)) { Error = TEXT("StableIdDuplicate:") + It->GetPathName(); return false; }
		Used.Add(Snapshot->StableId);
		if (It->GetParentActor() && bRequireIds)
		{
			int32 Count = 0;
			for (FName Tag : It->Tags)
			{
				const FString Value = Tag.ToString();
				if (!Value.StartsWith(TEXT("SWRoomStableId="))) continue;
				FGuid Id; ++Count;
				if (!FGuid::Parse(Value.Mid(15), Id) || Id != Snapshot->StableId) { Error = TEXT("ChildTagIdMismatch:") + It->GetPathName(); return false; }
			}
			if (Count != 1) { Error = TEXT("ChildTagIdMissing:") + It->GetPathName(); return false; }
		}
	}
	return true;
}

FString ObjectKey(const UObject* Object, const FLogicalActorKeys& LogicalActorKeys)
{
	const AActor* Actor = Cast<AActor>(Object);
	if (!Actor) Actor = Object ? Object->GetTypedOuter<AActor>() : nullptr;
	if (!Actor) return GetPathNameSafe(Object);
	FString ActorKey;
	if (const AActor* Parent = Actor->GetParentActor())
	{
		TInlineComponentArray<UChildActorComponent*> Children(Parent);
		for (UChildActorComponent* Child : Children)
			if (Child && Child->GetChildActor() == Actor) { ActorKey = ObjectKey(Parent, LogicalActorKeys) + TEXT("/") + Child->GetName(); break; }
	}
	if (ActorKey.IsEmpty())
	{
		const FString* LogicalKey = LogicalActorKeys.Find(TWeakObjectPtr<AActor>(const_cast<AActor*>(Actor)));
		ActorKey = LogicalKey ? *LogicalKey : Actor->GetPathName();
	}
	return Object == Actor ? ActorKey : ActorKey + TEXT("/") + Object->GetPathName(Actor);
}

bool CollectChildActorsRecursive(AActor* Parent, TSet<TWeakObjectPtr<AActor>>& OutChildren, TSet<TWeakObjectPtr<AActor>>& Visiting)
{
	if (!IsValid(Parent) || Visiting.Contains(Parent)) return false;
	Visiting.Add(Parent);
	TInlineComponentArray<UChildActorComponent*> ChildComponents(Parent);
	for (UChildActorComponent* ChildComponent : ChildComponents)
	{
		AActor* Child = ChildComponent ? ChildComponent->GetChildActor() : nullptr;
		if (!IsValid(Child)) continue;
		if (Child->GetParentActor() != Parent || OutChildren.Contains(Child)) return false;
		OutChildren.Add(Child);
		if (!CollectChildActorsRecursive(Child, OutChildren, Visiting)) return false;
	}
	Visiting.Remove(Parent);
	return true;
}

void CaptureProperties(UObject* Object, const FPathAliases& Paths, const FLogicalActorKeys& LogicalActorKeys, TMap<FString, FString>& Out)
{
	if (!Object || Object->IsTemplate()) return;
	for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
	{
		FProperty* Property = *It;
		if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient | CPF_EditorOnly)) continue;
		const FName Name = Property->GetFName();
		if (Name == TEXT("Tags") || Name == TEXT("StableId")) continue;
		FString Value;
		Property->ExportText_InContainer(0, Value, Object, nullptr, Object, PPF_None);
		// Both loaded assets and verified backup worlds have absolute object
		// paths. Values without a slash cannot contain any WorldPaths alias.
		if (Value.Contains(TEXT("/"), ESearchCase::CaseSensitive))
			for (const auto& Path : Paths) Value.ReplaceInline(*Path.Key, *Path.Value, ESearchCase::CaseSensitive);
		Out.Add(ObjectKey(Object, LogicalActorKeys) + TEXT("|") + Property->GetName(), Value);
	}
}

void AddSoftReferenceState(const FSoftObjectPath& SoftPath, UObject* WorldContext, const FLogicalActorKeys& LogicalActorKeys,
	const FString& Key, FSoftReferenceMap& OutReferences)
{
	FSoftReferenceState State;
	State.Path = SoftPath.ToString();
	UObject* Resolved = SoftPath.ResolveObject();
	State.bResolved = IsValid(Resolved);
	if (State.bResolved)
	{
		AActor* TargetActor = Cast<AActor>(Resolved);
		if (!TargetActor) TargetActor = Resolved->GetTypedOuter<AActor>();
		if (TargetActor && TargetActor->GetWorld() == WorldContext->GetWorld() && !TargetActor->IsTemplate())
		{
			State.bWorldActorTarget = true;
			State.WorldTargetKey = ObjectKey(TargetActor, LogicalActorKeys);
			if (UActorComponent* TargetComponent = Cast<UActorComponent>(Resolved))
				State.WorldTargetKey = ObjectKey(TargetComponent, LogicalActorKeys);
		}
	}
	OutReferences.FindOrAdd(Key).Add(MoveTemp(State));
}

void CaptureSoftReferencesFromValue(FProperty* Property, const void* ValueAddress, UObject* WorldContext,
	const FLogicalActorKeys& LogicalActorKeys, const FString& PropertyPath, FSoftReferenceMap& OutReferences)
{
	if (!Property || !ValueAddress) return;
	if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
	{
		const FSoftObjectPtr SoftObject = SoftObjectProperty->GetPropertyValue(ValueAddress);
		AddSoftReferenceState(SoftObject.ToSoftObjectPath(), WorldContext, LogicalActorKeys, PropertyPath, OutReferences);
		return;
	}
	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (StructProperty->Struct == FSoftObjectPath::StaticStruct())
		{
			AddSoftReferenceState(*static_cast<const FSoftObjectPath*>(ValueAddress), WorldContext, LogicalActorKeys, PropertyPath, OutReferences);
			return;
		}
		for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
			CaptureSoftReferencesFromValue(*It, It->ContainerPtrToValuePtr<void>(ValueAddress), WorldContext, LogicalActorKeys,
				PropertyPath + TEXT(".") + It->GetName(), OutReferences);
		return;
	}
	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper Helper(ArrayProperty, ValueAddress);
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
			CaptureSoftReferencesFromValue(ArrayProperty->Inner, Helper.GetRawPtr(Index), WorldContext, LogicalActorKeys,
				PropertyPath + TEXT("[]"), OutReferences);
		return;
	}
	if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		FScriptSetHelper Helper(SetProperty, ValueAddress);
		for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
			if (Helper.IsValidIndex(Index)) CaptureSoftReferencesFromValue(SetProperty->ElementProp, Helper.GetElementPtr(Index), WorldContext,
				LogicalActorKeys, PropertyPath + TEXT("{}"), OutReferences);
		return;
	}
	if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		FScriptMapHelper Helper(MapProperty, ValueAddress);
		for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
			if (Helper.IsValidIndex(Index))
			{
				CaptureSoftReferencesFromValue(MapProperty->KeyProp, Helper.GetKeyPtr(Index), WorldContext, LogicalActorKeys,
					PropertyPath + TEXT("{key}"), OutReferences);
				CaptureSoftReferencesFromValue(MapProperty->ValueProp, Helper.GetValuePtr(Index), WorldContext, LogicalActorKeys,
					PropertyPath + TEXT("{value}"), OutReferences);
			}
	}
}

void CaptureSoftReferences(UObject* Object, const FLogicalActorKeys& LogicalActorKeys, FSoftReferenceMap& OutReferences)
{
	if (!Object || Object->IsTemplate()) return;
	const FString OwnerKey = ObjectKey(Object, LogicalActorKeys);
	for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		if (!It->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient | CPF_EditorOnly))
			CaptureSoftReferencesFromValue(*It, It->ContainerPtrToValuePtr<void>(Object), Object, LogicalActorKeys,
				OwnerKey + TEXT("|") + It->GetName(), OutReferences);
}

// Preserve authored world references by the same logical identities used by
// the existing semantic audit, rather than by names assigned during paste.
FString WorldReferenceKey(const UObject* Object, const FLogicalActorKeys& Keys)
{
	// A child actor and its parent ChildActorComponent share the same logical
	// path. Their actual types keep these two identity domains distinct.
	return Object->GetClass()->GetPathName() + TEXT("|") + ObjectKey(Object, Keys);
}

struct FReferencedActorSubobject
{
	TStrongObjectPtr<UObject> Object;
	FString ActorKey;
	TMap<FString, FString> Properties;
};

using FReferencedActorSubobjects = TMap<FString, FReferencedActorSubobject>;

bool ProcessHardReferenceValue(FProperty* Property, void* Address, UObject* Owner,
	const FLogicalActorKeys& LogicalKeys, const FString& Field, TMap<FString, FString>& References,
	const TMap<FString, UObject*>* Targets, FString& Error, FString& Report,
	FReferencedActorSubobjects* Subobjects)
{
	if (!Property || !Address || Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient | CPF_EditorOnly)) return true;
	if (CastField<FSoftObjectProperty>(Property)) return true;
	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		UObject* Current = ObjectProperty->GetObjectPropertyValue(Address);
		if (!Targets)
		{
			AActor* Actor = Cast<AActor>(Current);
			if (!Actor && Current) Actor = Current->GetTypedOuter<AActor>();
			if (Actor && !Actor->IsTemplate() && Actor->GetWorld() == Owner->GetWorld())
			{
				References.Add(Field, WorldReferenceKey(Current, LogicalKeys));
				// Retain only actually referenced, direct actor-owned data objects.
				// Construction scripts may recreate these with a different name
				// during editor paste. Preserve the original object data and name.
				if (Subobjects && Current->GetOuter() == Actor && Current != Actor
					&& !Current->IsA<UActorComponent>() && !Current->IsTemplate())
				{
					FReferencedActorSubobject& Entry = Subobjects->FindOrAdd(WorldReferenceKey(Current, LogicalKeys));
					Entry.Object.Reset(Current);
					Entry.ActorKey = WorldReferenceKey(Actor, LogicalKeys);
				}
				Report += FString::Printf(TEXT("OriginalWorldReference Field=%s Target=%s Class=%s Owner=%s Parent=%s EditorOnly=%d\n"),
					*Field, *ObjectKey(Current, LogicalKeys), *Current->GetClass()->GetPathName(),
					*GetPathNameSafe(Actor->GetOwner()), *GetPathNameSafe(Actor->GetAttachParentActor()), Actor->IsEditorOnly());
			}
			return true;
		}
		const FString* ExpectedKey = References.Find(Field);
		if (!ExpectedKey) return true;
		UObject* const* Target = Targets->Find(*ExpectedKey);
		// A referenced authored data subobject (for example spline metadata)
		// need not be a component. Accept its unchanged logical identity only
		// when its owning actor is the indexed current instance.
		if (!Target && IsValid(Current) && WorldReferenceKey(Current, LogicalKeys) == *ExpectedKey)
		{
			AActor* Actor = Current->GetTypedOuter<AActor>();
			UObject* const* CurrentActor = Actor ? Targets->Find(WorldReferenceKey(Actor, LogicalKeys)) : nullptr;
			if (CurrentActor && *CurrentActor == Actor) return true;
		}
		if (!Target || !IsValid(*Target) || !(*Target)->IsA(ObjectProperty->PropertyClass))
		{
			Error = TEXT("MigrationWorldReferenceTargetMissing:") + Field + TEXT(" Expected=") + *ExpectedKey
				+ TEXT(" Current=") + GetPathNameSafe(Current) + TEXT(" Type=") + GetPathNameSafe(ObjectProperty->PropertyClass);
			return false;
		}
		if (Current != *Target)
		{
			ObjectProperty->SetObjectPropertyValue(Address, *Target);
			Report += TEXT("PreservedWorldReference Field=") + Field + TEXT(" Target=") + *ExpectedKey + TEXT("\n");
		}
		return true;
	}
	if (FStructProperty* Struct = CastField<FStructProperty>(Property))
	{
		for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
			for (int32 Index = 0; Index < It->ArrayDim; ++Index)
				if (!ProcessHardReferenceValue(*It, It->ContainerPtrToValuePtr<void>(Address, Index), Owner, LogicalKeys,
					Field + TEXT(".") + It->GetName() + FString::Printf(TEXT("[%d]"), Index), References, Targets, Error, Report, Subobjects)) return false;
	}
	else if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper Helper(Array, Address);
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
			if (!ProcessHardReferenceValue(Array->Inner, Helper.GetRawPtr(Index), Owner, LogicalKeys,
				Field + FString::Printf(TEXT("[%d]"), Index), References, Targets, Error, Report, Subobjects)) return false;
	}
	else if (FSetProperty* Set = CastField<FSetProperty>(Property))
	{
		FScriptSetHelper Helper(Set, Address);
		for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
			if (Helper.IsValidIndex(Index) && !ProcessHardReferenceValue(Set->ElementProp, Helper.GetElementPtr(Index), Owner,
				LogicalKeys, Field + FString::Printf(TEXT("{%d}"), Index), References, Targets, Error, Report, Subobjects)) return false;
		if (Targets) Helper.Rehash();
	}
	else if (FMapProperty* Map = CastField<FMapProperty>(Property))
	{
		FScriptMapHelper Helper(Map, Address);
		for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
			if (Helper.IsValidIndex(Index))
			{
				if (!ProcessHardReferenceValue(Map->KeyProp, Helper.GetKeyPtr(Index), Owner, LogicalKeys,
						Field + FString::Printf(TEXT("{key%d}"), Index), References, Targets, Error, Report, Subobjects)
					|| !ProcessHardReferenceValue(Map->ValueProp, Helper.GetValuePtr(Index), Owner, LogicalKeys,
						Field + FString::Printf(TEXT("{value%d}"), Index), References, Targets, Error, Report, Subobjects)) return false;
			}
		if (Targets) Helper.Rehash();
	}
	return true;
}

bool ProcessHardReferences(UObject* Object, const FLogicalActorKeys& Keys, TMap<FString, FString>& References,
	const TMap<FString, UObject*>* Targets, FString& Error, FString& Report,
	FReferencedActorSubobjects* Subobjects = nullptr)
{
	if (!Object || Object->IsTemplate()) return true;
	for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		for (int32 Index = 0; Index < It->ArrayDim; ++Index)
			if (!ProcessHardReferenceValue(*It, It->ContainerPtrToValuePtr<void>(Object, Index), Object, Keys,
				ObjectKey(Object, Keys) + TEXT("|") + It->GetName() + FString::Printf(TEXT("[%d]"), Index),
				References, Targets, Error, Report, Subobjects)) return false;
	return true;
}

FString SoftReferenceStateKey(const FSoftReferenceState& State)
{
	return FString::Printf(TEXT("%d|%s|%s|%s"), State.bResolved, State.bWorldActorTarget ? TEXT("World") : TEXT("Other"),
		*State.WorldTargetKey, *State.Path);
}

void SortSoftReferenceMap(FSoftReferenceMap& References)
{
	for (auto& Pair : References)
		Pair.Value.Sort([](const FSoftReferenceState& Left, const FSoftReferenceState& Right)
		{
			return SoftReferenceStateKey(Left) < SoftReferenceStateKey(Right);
		});
}

bool CompareSoftReferenceMaps(FSoftReferenceMap Before, FSoftReferenceMap After, FString& OutDifference)
{
	OutDifference.Reset();
	SortSoftReferenceMap(Before); SortSoftReferenceMap(After);
	if (Before.Num() != After.Num()) { OutDifference = TEXT("SoftReferencePropertyCount"); return false; }
	for (const auto& Pair : Before)
	{
		const TArray<FSoftReferenceState>* NewStates = After.Find(Pair.Key);
		if (!NewStates || NewStates->Num() != Pair.Value.Num()) { OutDifference = Pair.Key + TEXT("Count"); return false; }
		for (int32 Index = 0; Index < Pair.Value.Num(); ++Index)
		{
			const FSoftReferenceState& OldState = Pair.Value[Index];
			const FSoftReferenceState& NewState = (*NewStates)[Index];
			if (OldState.bWorldActorTarget)
			{
				if (!NewState.bResolved || !NewState.bWorldActorTarget || OldState.WorldTargetKey != NewState.WorldTargetKey)
				{ OutDifference = Pair.Key + TEXT("WorldTarget"); return false; }
			}
			else if (OldState.Path != NewState.Path || OldState.bResolved != NewState.bResolved || NewState.bWorldActorTarget)
			{ OutDifference = Pair.Key + TEXT("AssetOrExternalTarget"); return false; }
		}
	}
	return true;
}

void CaptureActor(AActor* Actor, const FPathAliases& Paths, const FLogicalActorKeys& LogicalActorKeys, FActorBefore& Out)
{
	Out.Transform = Actor->GetActorTransform(); Out.Class = Actor->GetClass()->GetPathName(); Out.Tags = Actor->Tags;
	Out.SpawnCollisionHandling = Actor->SpawnCollisionHandlingMethod;
	if (const AActor* Parent = Actor->GetParentActor())
	{
		TInlineComponentArray<UChildActorComponent*> ChildComponents(Parent);
		for (UChildActorComponent* ChildComponent : ChildComponents)
			if (ChildComponent && ChildComponent->GetChildActor() == Actor)
			{
				Out.ParentKey = ObjectKey(Parent, LogicalActorKeys) + TEXT("/") + ChildComponent->GetName();
				break;
			}
		if (Out.ParentKey.IsEmpty()) Out.ParentKey = TEXT("<invalid-parent>");
	}
	if (USWRoomSnapshotComponent* Snapshot = Actor->FindComponentByClass<USWRoomSnapshotComponent>()) Out.StableId = Snapshot->StableId;
	CaptureProperties(Actor, Paths, LogicalActorKeys, Out.Properties);
	TInlineComponentArray<UActorComponent*> Components(Actor);
	for (UActorComponent* Component : Components)
		if (Component && !Component->IsEditorOnly())
		{
			const FString ComponentKey = ObjectKey(Component, LogicalActorKeys);
			if (Out.ComponentClasses.Contains(ComponentKey)) Out.ComponentClasses[ComponentKey] = TEXT("<duplicate-component-key>");
			else Out.ComponentClasses.Add(ComponentKey, Component->GetClass()->GetPathName());
			CaptureProperties(Component, Paths, LogicalActorKeys, Out.Properties);
			if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
			{
				Out.ComponentCollisionModes.Add(ComponentKey, Primitive->BodyInstance.GetCollisionEnabled(false));
				// Primitive PostInitProperties applies constructor-deferred profiles,
				// and PostLoad fixes up loaded body settings. A commandlet does not
				// need a live physics body to preserve these authored properties.
				if (!Primitive->HasAnyFlags(RF_NeedInitialization | RF_NeedPostLoad))
				{
					TSharedPtr<FBodyInstance> CollisionProperties = MakeShared<FBodyInstance>();
					CollisionProperties->CopyRuntimeBodyInstancePropertiesFrom(&Primitive->BodyInstance);
					Out.ComponentCollisionProperties.Add(ComponentKey, MoveTemp(CollisionProperties));
				}
				Out.Properties.Add(ComponentKey + TEXT("|BodyInstanceCollisionEnabledValue"),
					FString::FromInt(static_cast<int32>(Primitive->BodyInstance.GetCollisionEnabled(false))));
			}
		}
}

FPathAliases WorldPaths(UWorld* World, const FLogicalActorKeys& LogicalActorKeys)
{
	FPathAliases Paths;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		Paths.Emplace(It->GetPathName(), ObjectKey(*It, LogicalActorKeys));
		TInlineComponentArray<UActorComponent*> Components(*It);
		for (UActorComponent* Component : Components) if (Component) Paths.Emplace(Component->GetPathName(), ObjectKey(Component, LogicalActorKeys));
	}
	Paths.Sort([](const auto& Left, const auto& Right) { return Left.Key.Len() > Right.Key.Len(); });
	return Paths;
}

bool HasCrossLevelReferences(UWorld* World, FString& Error)
{
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TArray<UObject*> Objects; Objects.Add(*It);
		TInlineComponentArray<UActorComponent*> Components(*It);
		for (UActorComponent* Component : Components) Objects.Add(Component);
		for (UObject* Object : Objects)
		{
			TArray<UObject*> References;
			FReferenceFinder Finder(References, nullptr, false, true, false, true); Finder.FindReferences(Object);
			for (UObject* Reference : References)
			{
				AActor* Target = Cast<AActor>(Reference);
				if (!Target) Target = Reference ? Reference->GetTypedOuter<AActor>() : nullptr;
				if (Target && Target->GetWorld() == World && !Target->IsTemplate() && Target->GetLevel() != It->GetLevel())
				{ Error = FString::Printf(TEXT("CrossLevelReference %s -> %s"), *Object->GetPathName(), *Target->GetPathName()); return true; }
			}
		}
	}
	return false;
}

struct FScopedMigration
{
	FScopedMigration() { USWRoomSnapshotComponent::BeginEditorVoyageMigration(); }
	~FScopedMigration() { USWRoomSnapshotComponent::EndEditorVoyageMigration(); }
};

struct FScopedMigrationIdentityTags
{
	UWorld* World = nullptr;
	TMap<FGuid, TArray<FName>> OriginalTagsById;
	~FScopedMigrationIdentityTags()
	{
		if (!IsValid(World)) return;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor)) continue;
			for (FName Tag : Actor->Tags)
			{
				const FString Value = Tag.ToString();
				if (!Value.StartsWith(TEXT("SWMigrationId="))) continue;
				FGuid Id;
				if (FGuid::Parse(Value.Mid(14), Id))
					if (const TArray<FName>* Original = OriginalTagsById.Find(Id)) Actor->Tags = *Original;
				break;
			}
		}
	}
};
}

USWVoyageLevelMigrationCommandlet::USWVoyageLevelMigrationCommandlet()
{
	IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true; ShowErrorCount = true;
}

void USWVoyageLevelMigrationCommandlet::CreateCustomEngine(const FString& Params)
{
	if (!GConfig) return;
	if (!bEngineConfigOverridden)
	{
		bEngineConfigKeyExisted = GConfig->GetString(TEXT("/Script/Engine.Engine"), TEXT("EditorEngine"), PreviousEditorEngine, GEngineIni);
		bEngineConfigOverridden = true;
	}
	GConfig->SetString(TEXT("/Script/Engine.Engine"), TEXT("EditorEngine"), TEXT("/Script/UnrealEd.UnrealEdEngine"), GEngineIni);
}

int32 USWVoyageLevelMigrationCommandlet::Main(const FString& Params)
{
	FScopedEngineConfigRestore ConfigRestore;
	const bool bApply = FParse::Param(*Params, TEXT("Apply"));
	const bool bDryRun = FParse::Param(*Params, TEXT("DryRun"));
	FString SourceMap, GameplayMap, ProfileAsset, ManifestPath;
	FParse::Value(*Params, TEXT("SourceMap="), SourceMap);
	FParse::Value(*Params, TEXT("GameplayMap="), GameplayMap);
	FParse::Value(*Params, TEXT("ProfileAsset="), ProfileAsset);
	FParse::Value(*Params, TEXT("ManifestPath="), ManifestPath);
	if (bApply == bDryRun || SourceMap.IsEmpty() || GameplayMap.IsEmpty() || ProfileAsset.IsEmpty() || ManifestPath.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Usage: -run=SWVoyageLevelMigration -DryRun|-Apply -SourceMap=/Game/... -GameplayMap=/Game/... -ProfileAsset=/Game/... -ManifestPath=<project-relative file>"));
		return 1;
	}
	for (const FString* Package : { &SourceMap, &GameplayMap, &ProfileAsset })
		if (!FPackageName::IsValidLongPackageName(*Package) || !Package->StartsWith(TEXT("/Game/"))) return 1;
	if (SourceMap == GameplayMap || SourceMap == ProfileAsset || GameplayMap == ProfileAsset) return 1;
	const FString ManifestCandidate = FPaths::IsRelative(ManifestPath) ? FPaths::Combine(FPaths::ProjectDir(), ManifestPath) : ManifestPath;
	const FString ManifestFullPath = FPaths::ConvertRelativePathToFull(ManifestCandidate);
	const FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	if (!ManifestFullPath.StartsWith(ProjectRoot, ESearchCase::IgnoreCase) || !IFileManager::Get().FileExists(*ManifestFullPath)) return 1;
	const FString OutputDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Diagnostics/VoyageMigration")));
	IFileManager::Get().MakeDirectory(*OutputDir, true);
	FString Report = FString::Printf(TEXT("Apply=%d\n"), bApply);
	Report += TEXT("ManifestPath=") + ManifestFullPath + TEXT("\n");
	const auto Finish = [&](int32 Code, const FString& Message)
	{
		Report += FString::Printf(TEXT("Result=%d Message=%s\n"), Code, *Message);
		FFileHelper::SaveStringToFile(Report, *FPaths::Combine(OutputDir, TEXT("Migration.txt")));
		if (Code == 0 && Message == TEXT("AppliedValidated SavedPackages=3"))
			FFileHelper::SaveStringToFile(Report, *FPaths::Combine(OutputDir, TEXT("Applied.txt")));
		UE_LOG(LogTemp, Display, TEXT("VoyageMigration Result=%d %s Output=%s"), Code, *Message, *OutputDir);
		return Code;
	};
	if (!GEditor || !GEditor->IsA<UUnrealEdEngine>()) return Finish(1, TEXT("UnrealEdEngineRequired"));
	Report += TEXT("EngineClass=") + GEditor->GetClass()->GetPathName() + TEXT("\n");
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *ManifestFullPath)) return Finish(1, TEXT("ManifestMissing"));
	const FString ExpectedHeader = TEXT("Path\tLabel\tClass\tLevel\tAttachParent\tTags\tLocation\tComponents");
	if (Lines.IsEmpty() || Lines[0] != ExpectedHeader) return Finish(1, TEXT("ManifestHeaderInvalid"));
	TArray<FMigrationRow> Rows;
	TSet<FString> Names;
	for (int32 Index = 1; Index < Lines.Num(); ++Index)
	{
		if (Lines[Index].IsEmpty()) continue;
		TArray<FString> Cells; Lines[Index].ParseIntoArray(Cells, TEXT("\t"), false);
		if (Cells.Num() != 8) return Finish(1, TEXT("ManifestColumnsInvalid"));
		FMigrationRow Row; Row.Path = Cells[0]; Row.Class = Cells[2]; Row.Tags = Cells[5]; Row.Location = Cells[6];
		if (!Row.Path.StartsWith(SourceMap + TEXT("."), ESearchCase::CaseSensitive)) return Finish(1, TEXT("ManifestSourceMapMismatch:") + Row.Path);
		int32 Dot; if (!Row.Path.FindLastChar(TEXT('.'), Dot)) return Finish(1, TEXT("ManifestPathInvalid"));
		Row.Name = Row.Path.Mid(Dot + 1);
		if (Names.Contains(Row.Name)) return Finish(1, TEXT("ManifestDuplicate"));
		Names.Add(Row.Name); Rows.Add(MoveTemp(Row));
	}
	if (Rows.IsEmpty()) return Finish(1, TEXT("ManifestRootCountInvalid"));
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(SourceMap);
	if (!World || World->HasBegunPlay() || World->WorldType != EWorldType::Editor || World->PersistentLevel->GetWorldPartition()) return Finish(1, TEXT("EditorWorldInvalid"));
	ULevelStreaming* Gameplay = nullptr;
	for (ULevelStreaming* Streaming : World->GetStreamingLevels())
		if (Streaming && UWorld::RemovePIEPrefix(Streaming->GetWorldAssetPackageName()) == GameplayMap)
		{ if (Gameplay) return Finish(1, TEXT("DuplicateGameplay")); Gameplay = Streaming; }
	const bool bMigrated = Gameplay != nullptr;
	if (Gameplay && !Gameplay->GetLoadedLevel())
	{
		Gameplay->SetShouldBeLoaded(true); Gameplay->SetShouldBeVisible(true); World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
	}
	TArray<AActor*> Roots;
	TSet<TWeakObjectPtr<AActor>> MatchedRoots;
	for (const FMigrationRow& Row : Rows)
	{
		TArray<AActor*> Candidates;
		FGuid ExpectedStableId;
		const bool bHasStableId = GetManifestStableId(Row, ExpectedStableId);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (bMigrated && It->GetClass()->GetPathName() == Row.Class)
				Report += FString::Printf(TEXT("ReloadManifestCandidate Expected=%s Actual=%s Level=%s Parent=%s Location=%s ExpectedLocation=%s Tags=%s ExpectedTags=%s TagsMatch=%d\n"),
					*Row.Path, *It->GetPathName(), *GetPathNameSafe(It->GetLevel()), *GetPathNameSafe(It->GetParentActor()),
					*It->GetActorLocation().ToCompactString(), *Row.Location, *TagsToString(It->Tags), *Row.Tags, TagsMatch(*It, Row.Tags, true));
			if (It->GetLevel() != (Gameplay ? Gameplay->GetLoadedLevel() : World->PersistentLevel.Get()) || It->GetParentActor()
				|| It->GetClass()->GetPathName() != Row.Class) continue;
			if (!bMigrated && It->GetName() != Row.Name) continue;
			if (bMigrated)
			{
				if (bHasStableId)
				{
					const USWRoomSnapshotComponent* Snapshot = It->FindComponentByClass<USWRoomSnapshotComponent>();
					if (!Snapshot || Snapshot->StableId != ExpectedStableId) continue;
				}
				else if (It->GetActorLocation().ToCompactString() != Row.Location || !TagsMatch(*It, Row.Tags, true)) continue;
			}
			Candidates.Add(*It);
		}
		if (bMigrated && Candidates.Num() != 1) return Finish(1, FString::Printf(TEXT("MigratedManifestCandidateCount:%s:%d"), *Row.Path, Candidates.Num()));
		if (!bMigrated && Candidates.Num() > 1) return Finish(1, TEXT("DuplicateManifestActor:") + Row.Name);
		AActor* Found = Candidates.IsEmpty() ? nullptr : Candidates[0];
		if (!Found || Found->GetParentActor() || Found->GetClass()->GetPathName() != Row.Class || Found->GetActorLocation().ToCompactString() != Row.Location
			|| !TagsMatch(Found, Row.Tags, bMigrated) || Found->GetLevel() != (Gameplay ? Gameplay->GetLoadedLevel() : World->PersistentLevel.Get()))
			return Finish(1, TEXT("ManifestActorMismatch:") + Row.Path);
		if (MatchedRoots.Contains(Found)) return Finish(1, TEXT("ManifestRootMatchedTwice:") + Row.Path);
		MatchedRoots.Add(Found);
		Roots.Add(Found);
	}
	TSet<TWeakObjectPtr<AActor>> ChildSet;
	TSet<TWeakObjectPtr<AActor>> ChildVisiting;
	for (AActor* Root : Roots)
		if (!CollectChildActorsRecursive(Root, ChildSet, ChildVisiting)) return Finish(1, TEXT("ChildActorRelationshipInvalid"));
	for (TActorIterator<AActor> It(World); It; ++It)
		if (It->GetParentActor() && Roots.Contains(It->GetParentActor()) && !ChildSet.Contains(*It)) return Finish(1, TEXT("UntrackedChildActor"));
	const int32 ExpectedRootCount = Rows.Num();
	const int32 ExpectedChildCount = ChildSet.Num();
	FScopedVoyageSelection SelectionScope;
	FString SelectionError;
	if (!SelectionScope.Initialize(SelectionError)) return Finish(1, SelectionError);
	SelectionScope.Selection->ClearSelection(FTypedElementSelectionOptions());
	int32 HandleCount = 0, SelectableCount = 0;
	for (AActor* Root : Roots)
	{
		const FTypedElementHandle Handle = UEngineElementsLibrary::AcquireEditorActorElementHandle(Root, true);
		if (!Handle) return Finish(1, TEXT("ActorElementHandleInvalid:") + Root->GetPathName());
		++HandleCount;
		if (!GEditor->CanSelectActor(Root, true, true, false)) return Finish(1, TEXT("ActorCannotBeSelected:") + Root->GetPathName());
		++SelectableCount; GEditor->SelectActor(Root, true, false, true);
	}
	SelectionScope.Selection->NotifyPendingChanges();
	Report += FString::Printf(TEXT("SourceRoots=%d HandleValidCount=%d CanSelectCount=%d SelectedCount=%d OldActorSet=%s OldComponentSet=%s NewSet=%s\n"),
		Roots.Num(), HandleCount, SelectableCount, GEditor->GetSelectedActorCount(), *GetNameSafe(SelectionScope.OriginalActors.Get()), *GetNameSafe(SelectionScope.OriginalComponents.Get()), *GetNameSafe(SelectionScope.Selection.Get()));
	if (GEditor->GetSelectedActorCount() != ExpectedRootCount) return Finish(1, TEXT("SourceSelectionCountInvalid"));
	FString IdentityError;
	if (!ValidateIds(World, bMigrated, IdentityError)) return Finish(1, IdentityError);
	const FString PersistentFile = FPackageName::LongPackageNameToFilename(SourceMap, FPackageName::GetMapPackageExtension());
	const FString GameplayFile = FPackageName::LongPackageNameToFilename(GameplayMap, FPackageName::GetMapPackageExtension());
	const FString ProfileFile = FPackageName::LongPackageNameToFilename(ProfileAsset, FPackageName::GetAssetPackageExtension());
	FString PersistentHash, HashError;
	if (!FileHash(PersistentFile, PersistentHash, HashError)) return Finish(1, HashError);
	Report += TEXT("SourcePackageHash=") + PersistentHash + TEXT("\n");
	TArray<FName> ScriptPackages;
	const FProjectDescriptor* Descriptor = IProjectManager::Get().GetCurrentProject();
	if (!Descriptor) return Finish(1, TEXT("ProjectDescriptorMissing"));
	for (const FModuleDescriptor& Module : Descriptor->Modules)
		if (Module.Type == EHostType::Runtime || Module.Type == EHostType::RuntimeNoCommandlet || Module.Type == EHostType::RuntimeAndProgram)
			ScriptPackages.Add(FName(*(TEXT("/Script/") + Module.Name.ToString())));
	ScriptPackages.Sort(FNameLexicalLess());
	if (bMigrated)
	{
		ASWVoyageResetAnchor* Anchor = nullptr;
		for (TActorIterator<ASWVoyageResetAnchor> It(World); It; ++It) { if (Anchor) return Finish(1, TEXT("AnchorDuplicate")); Anchor = *It; }
		FString Error;
		const ULevelStreamingDynamic* Dynamic = Cast<ULevelStreamingDynamic>(Gameplay);
		if (!Gameplay->IsA<ULevelStreamingDynamic>() || !Gameplay->GetLoadedLevel() || Gameplay->GetLoadedLevel()->GetWorldPartition()
			|| !Gameplay->LevelTransform.Equals(FTransform::Identity)
			|| !Dynamic || !Dynamic->bInitiallyLoaded || !Dynamic->bInitiallyVisible
			|| !Gameplay->ShouldBeLoaded() || !Gameplay->ShouldBeVisible() || !Gameplay->EditorStreamingVolumes.IsEmpty() || !Anchor
			|| Anchor->GetLevel() != World->PersistentLevel || !Anchor->Profile || Anchor->Profile->GetPathName() != ProfileAsset + TEXT(".") + FPackageName::GetShortName(ProfileAsset)
			|| Anchor->Profile->GameplayLevel.ToSoftObjectPath().GetLongPackageName() != GameplayMap
			|| Anchor->Profile->ProjectScriptPackages != ScriptPackages || !Anchor->Profile->ValidateProfile(Error) || HasCrossLevelReferences(World, Error)) return Finish(1, TEXT("MigratedContractInvalid:") + Error);
		for (const FString& File : { PersistentFile, GameplayFile, ProfileFile })
		{
			FString Hash;
			if (!IFileManager::Get().FileExists(*File) || !FileHash(File, Hash, HashError)) return Finish(1, HashError.IsEmpty() ? TEXT("MigratedPackageMissing:") + File : HashError);
			Report += TEXT("MigratedPackageHash File=") + File + TEXT(" Hash=") + Hash + TEXT("\n");
		}
		// Reuse the original semantic captures against the verified pre-Apply
		// backup in this fresh process. No package is saved by this audit.
		TArray<FString> ApplyLines;
		if (!FFileHelper::LoadFileToStringArray(ApplyLines, *FPaths::Combine(OutputDir, TEXT("Applied.txt"))))
			return Finish(1, TEXT("ReloadOriginalApplyReportMissing"));
		FString OriginalBackup, OriginalHash;
		bool bApplySucceeded = false;
		for (const FString& Line : ApplyLines)
		{
			if (Line.StartsWith(TEXT("Backup="))) OriginalBackup = Line.RightChop(7);
			if (Line.StartsWith(TEXT("SourcePackageHash="))) OriginalHash = Line.RightChop(18);
			bApplySucceeded |= Line == TEXT("Result=0 Message=AppliedValidated SavedPackages=3");
		}
		const FString BackupRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Diagnostics/VoyageMigration_Backup/")));
		OriginalBackup = FPaths::ConvertRelativePathToFull(OriginalBackup);
		if (!bApplySucceeded || OriginalHash.IsEmpty() || !OriginalBackup.StartsWith(BackupRoot, ESearchCase::IgnoreCase))
			return Finish(1, TEXT("ReloadOriginalBackupReportInvalid"));
		const FString OriginalFile = FPaths::Combine(OriginalBackup, FPaths::GetCleanFilename(PersistentFile));
		FString BackupHash;
		if (!FileHash(OriginalFile, BackupHash, HashError) || !BackupHash.Equals(OriginalHash, ESearchCase::IgnoreCase))
			return Finish(1, TEXT("ReloadOriginalBackupHashMismatch:") + HashError);
		FLogicalActorKeys ReloadKeys;
		for (TActorIterator<AActor> It(World); It; ++It)
			if (IsValid(*It)) ReloadKeys.Add(*It, It->GetPathName());
		for (int32 Index = 0; Index < Roots.Num(); ++Index) ReloadKeys.FindChecked(Roots[Index]) = Rows[Index].Path;
		const FPathAliases ReloadPaths = WorldPaths(World, ReloadKeys);
		TMap<FString, FActorBefore> ReloadActors;
		FSoftReferenceMap ReloadSoftReferences;
		TMap<FString, FString> ReloadHardReferences;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (IsHelper(*It) || *It == Anchor) continue;
			CaptureActor(*It, ReloadPaths, ReloadKeys, ReloadActors.Add(ObjectKey(*It, ReloadKeys)));
			CaptureSoftReferences(*It, ReloadKeys, ReloadSoftReferences);
			if (!ProcessHardReferences(*It, ReloadKeys, ReloadHardReferences, nullptr, Error, Report)) return Finish(1, Error);
			TInlineComponentArray<UActorComponent*> Components(*It);
			for (UActorComponent* Component : Components)
				if (Component && !Component->IsEditorOnly())
				{
					CaptureSoftReferences(Component, ReloadKeys, ReloadSoftReferences);
					if (!ProcessHardReferences(Component, ReloadKeys, ReloadHardReferences, nullptr, Error, Report)) return Finish(1, Error);
				}
		}
		UWorld* OriginalWorld = UEditorLoadingAndSavingUtils::LoadMap(OriginalFile);
		if (!OriginalWorld || OriginalWorld->HasBegunPlay() || OriginalWorld->WorldType != EWorldType::Editor)
			return Finish(1, TEXT("ReloadOriginalBackupWorldInvalid"));
		FLogicalActorKeys OriginalKeys;
		const FString OriginalActorPrefix = SourceMap + TEXT(".") + FPackageName::GetShortName(SourceMap) + TEXT(":PersistentLevel.");
		for (TActorIterator<AActor> It(OriginalWorld); It; ++It)
			if (IsValid(*It)) OriginalKeys.Add(*It, OriginalActorPrefix + It->GetName());
		const FPathAliases OriginalPaths = WorldPaths(OriginalWorld, OriginalKeys);
		FSoftReferenceMap OriginalSoftReferences;
		TMap<FString, FString> OriginalHardReferences;
		int32 ComparedActors = 0;
		for (TActorIterator<AActor> It(OriginalWorld); It; ++It)
		{
			if (IsHelper(*It)) continue;
			const FString Key = ObjectKey(*It, OriginalKeys);
			FActorBefore Original;
			CaptureActor(*It, OriginalPaths, OriginalKeys, Original);
			const FActorBefore* Reloaded = ReloadActors.Find(Key);
			if (!Reloaded || Original.Class != Reloaded->Class || !Original.Transform.Equals(Reloaded->Transform)
				|| Original.ParentKey != Reloaded->ParentKey || !Original.ComponentClasses.OrderIndependentCompareEqual(Reloaded->ComponentClasses)
				|| !TagsMatchAfterMigration(Original.Tags, Reloaded->Tags, !Original.StableId.IsValid() && Reloaded->StableId.IsValid())
				|| (Original.StableId.IsValid() && Original.StableId != Reloaded->StableId))
				return Finish(1, TEXT("ReloadActorSemanticMismatch:") + Key);
			for (const auto& Property : Original.Properties)
			{
				const FString* Actual = Reloaded->Properties.Find(Property.Key);
				if (!Actual || *Actual != Property.Value)
				{
					Report += TEXT("ReloadPropertyDifference Field=") + Property.Key + TEXT(" Before=") + Property.Value
						+ TEXT(" After=") + (Actual ? *Actual : TEXT("<missing>")) + TEXT("\n");
					return Finish(1, TEXT("ReloadReflectedPropertyMismatch"));
				}
			}
			++ComparedActors;
			CaptureSoftReferences(*It, OriginalKeys, OriginalSoftReferences);
			if (!ProcessHardReferences(*It, OriginalKeys, OriginalHardReferences, nullptr, Error, Report)) return Finish(1, Error);
			TInlineComponentArray<UActorComponent*> Components(*It);
			for (UActorComponent* Component : Components)
				if (Component && !Component->IsEditorOnly())
				{
					CaptureSoftReferences(Component, OriginalKeys, OriginalSoftReferences);
					if (!ProcessHardReferences(Component, OriginalKeys, OriginalHardReferences, nullptr, Error, Report)) return Finish(1, Error);
				}
		}
		if (ComparedActors != ReloadActors.Num()) return Finish(1, TEXT("ReloadActorSetCountMismatch"));
		if (!OriginalHardReferences.OrderIndependentCompareEqual(ReloadHardReferences)) return Finish(1, TEXT("ReloadHardReferenceMismatch"));
		if (!CompareSoftReferenceMaps(OriginalSoftReferences, ReloadSoftReferences, Error)) return Finish(1, TEXT("ReloadSoftReferenceMismatch:") + Error);
		Report += FString::Printf(TEXT("ReloadSemanticAudit Actors=%d Roots=%d Children=%d BackupHash=%s\n"), ComparedActors, ExpectedRootCount, ExpectedChildCount, *BackupHash);
		return Finish(0, TEXT("AlreadyMigratedNoChanges SavedAssets=0"));
	}
	FString Error;
	if (HasCrossLevelReferences(World, Error)) return Finish(1, Error);
	if (!bApply) return Finish(0, FString::Printf(TEXT("DryRunValidatedRoots=%d Children=%d SavedAssets=0"), ExpectedRootCount, ExpectedChildCount));
	const FString Backup = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Diagnostics/VoyageMigration_Backup"), FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"))));
	if (!IFileManager::Get().MakeDirectory(*Backup, true)) return Finish(1, TEXT("BackupDirectoryFailed"));
	Report += TEXT("Backup=") + Backup + TEXT("\n");
	for (const FString& File : { PersistentFile, GameplayFile, ProfileFile })
	{
		const bool bExists = IFileManager::Get().FileExists(*File);
		FString Hash = TEXT("Missing");
		if (bExists && !FileHash(File, Hash, HashError)) return Finish(1, HashError);
		Report += FString::Printf(TEXT("Before File=%s Exists=%d Hash=%s\n"), *File, bExists, *Hash);
		if (!bExists) continue;
		const FString BackupFile = FPaths::Combine(Backup, FPaths::GetCleanFilename(File));
		if (IFileManager::Get().Copy(*BackupFile, *File, false) != COPY_OK) return Finish(1, TEXT("BackupFailed:") + File);
		FString BackupHash;
		if (!FileHash(BackupFile, BackupHash, HashError)) return Finish(1, HashError);
		if (!Hash.Equals(BackupHash, ESearchCase::IgnoreCase)) return Finish(1, TEXT("BackupHashMismatch:") + BackupFile);
		Report += TEXT("BackupHashVerified=") + BackupFile + TEXT("\n");
	}
	if (IFileManager::Get().FileExists(*ProfileFile)) return Finish(1, TEXT("PartialProfileAlreadyExists"));
	TSet<FString> DirtyPackagesBefore;
	for (TObjectIterator<UPackage> It; It; ++It)
		if (It->IsDirty()) DirtyPackagesBefore.Add(It->GetName());
	TMap<FString, FActorBefore> Before;
	FSoftReferenceMap BeforeSoftReferences;
	TMap<FString, FString> BeforeWorldReferences;
	FReferencedActorSubobjects BeforeActorSubobjects;
	FLogicalActorKeys SourceLogicalKeys;
	TSet<FString> UniqueLogicalKeys;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (IsHelper(*It)) continue;
		const FString Key = It->GetPathName();
		if (UniqueLogicalKeys.Contains(Key)) return Finish(1, TEXT("LogicalActorKeyDuplicate:") + Key);
		UniqueLogicalKeys.Add(Key); SourceLogicalKeys.Add(*It, Key);
	}
	const FPathAliases OriginalPaths = WorldPaths(World, SourceLogicalKeys);
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (IsHelper(*It)) continue;
		CaptureActor(*It, OriginalPaths, SourceLogicalKeys, Before.Add(ObjectKey(*It, SourceLogicalKeys)));
		CaptureSoftReferences(*It, SourceLogicalKeys, BeforeSoftReferences);
		if (!ProcessHardReferences(*It, SourceLogicalKeys, BeforeWorldReferences, nullptr, Error, Report, &BeforeActorSubobjects)) return Finish(1, Error);
		TInlineComponentArray<UActorComponent*> Components(*It);
		for (UActorComponent* Component : Components)
			if (Component && !Component->IsEditorOnly())
			{
				CaptureSoftReferences(Component, SourceLogicalKeys, BeforeSoftReferences);
				if (!ProcessHardReferences(Component, SourceLogicalKeys, BeforeWorldReferences, nullptr, Error, Report, &BeforeActorSubobjects)) return Finish(1, Error);
			}
	}
	FScopedMigrationIdentityTags MigrationIdentityScope;
	for (auto& Pair : BeforeActorSubobjects)
		CaptureProperties(Pair.Value.Object.Get(), OriginalPaths, SourceLogicalKeys, Pair.Value.Properties);
	MigrationIdentityScope.World = World;
	TMap<FGuid, FString> MigrationSourceKeys;
	for (AActor* Root : Roots)
	{
		for (FName Tag : Root->Tags)
			if (Tag.ToString().StartsWith(TEXT("SWMigrationId="))) return Finish(1, TEXT("MigrationTagAlreadyPresent:") + Root->GetPathName());
		const FGuid MigrationId = FGuid::NewGuid();
		const FString SourceKey = SourceLogicalKeys.FindChecked(Root);
		MigrationSourceKeys.Add(MigrationId, SourceKey);
		MigrationIdentityScope.OriginalTagsById.Add(MigrationId, Root->Tags);
		Root->Tags.Add(FName(*(TEXT("SWMigrationId=") + MigrationId.ToString(EGuidFormats::Digits))));
	}
	FScopedMigration Scope;
	if (IFileManager::Get().FileExists(*GameplayFile))
	{
		UPackage* Existing = LoadPackage(nullptr, *GameplayMap, LOAD_None);
		UWorld* ExistingWorld = Existing ? UWorld::FindWorldInPackage(Existing) : nullptr;
		if (!ExistingWorld || !ExistingWorld->PersistentLevel || ExistingWorld->PersistentLevel->GetWorldPartition()
			|| !ExistingWorld->GetStreamingLevels().IsEmpty()) return Finish(1, TEXT("ExistingGameplayContractInvalid"));
		for (TActorIterator<AActor> It(ExistingWorld); It; ++It)
			if (!IsHelper(*It)) return Finish(1, TEXT("PartialGameplayNotEmpty:") + It->GetPathName());
		Gameplay = UEditorLevelUtils::AddLevelToWorld(World, *GameplayMap, ULevelStreamingDynamic::StaticClass());
	}
	else
	{
		UEditorLevelUtils::FCreateNewStreamingLevelForWorldParams Create(ULevelStreamingDynamic::StaticClass(), GameplayFile);
		Create.bUseExternalActors = false; Create.bCreateWorldPartition = false; Create.bUseSaveAs = false;
		Create.LevelStreamingCreatedCallback = [](ULevelStreaming* Streaming)
		{
			Streaming->LevelTransform = FTransform::Identity;
			if (ULevelStreamingDynamic* Dynamic = Cast<ULevelStreamingDynamic>(Streaming)) { Dynamic->bInitiallyLoaded = true; Dynamic->bInitiallyVisible = true; }
			Streaming->SetShouldBeLoaded(true); Streaming->SetShouldBeVisible(true);
		};
		Gameplay = UEditorLevelUtils::CreateNewStreamingLevelForWorld(*World, Create);
		Report += TEXT("PreliminaryGameplaySave=") + GameplayFile + TEXT("\n");
	}
	if (!Gameplay || !Gameplay->GetLoadedLevel() || !IFileManager::Get().FileExists(*GameplayFile)) return Finish(1, TEXT("GameplayCreationFailed"));
	if (ULevelStreamingDynamic* Dynamic = Cast<ULevelStreamingDynamic>(Gameplay)) { Dynamic->bInitiallyLoaded = true; Dynamic->bInitiallyVisible = true; }
	Gameplay->SetShouldBeLoaded(true); Gameplay->SetShouldBeVisible(true); Gameplay->EditorStreamingVolumes.Reset();
	TArray<AActor*> Moved;
	const int32 MovedCount = UEditorLevelUtils::MoveActorsToLevel(Roots, Gameplay->GetLoadedLevel(), false, false, true, &Moved);
	Report += FString::Printf(TEXT("MoveReturnCount=%d OutActorsCount=%d\n"), MovedCount, Moved.Num());
	for (AActor* Actor : Moved)
		if (IsValid(Actor))
		{
			Report += FString::Printf(TEXT("MovedActor Name=%s Class=%s Transform=%s Tags=%s Parent=%s\n"),
				*Actor->GetName(), *Actor->GetClass()->GetPathName(), *Actor->GetActorTransform().ToHumanReadableString(), *ManifestTags(Actor, false), *GetNameSafe(Actor->GetParentActor()));
		}
	TArray<AActor*> Discovered;
	TMap<FGuid, TWeakObjectPtr<AActor>> DestinationActorsById;
	int32 SourceIdentityCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!IsValid(*It)) continue;
		TArray<FGuid> ActorMigrationIds;
		for (FName Tag : It->Tags)
		{
			const FString TagValue = Tag.ToString();
			if (!TagValue.StartsWith(TEXT("SWMigrationId="))) continue;
			FGuid Id;
			if (!FGuid::Parse(TagValue.Mid(14), Id) || !Id.IsValid()) return Finish(1, TEXT("MigrationIdMalformed:") + It->GetPathName());
			ActorMigrationIds.Add(Id);
		}
		if (ActorMigrationIds.IsEmpty()) continue;
		if (ActorMigrationIds.Num() != 1 || !MigrationSourceKeys.Contains(ActorMigrationIds[0])) return Finish(1, TEXT("MigrationIdUnregistered:") + It->GetPathName());
		const FGuid Id = ActorMigrationIds[0];
		if (It->GetParentActor()) return Finish(1, TEXT("MigrationIdentityOnChildActor:") + It->GetPathName());
		if (DestinationActorsById.Contains(Id)) return Finish(1, TEXT("MigrationIdDuplicate:") + Id.ToString(EGuidFormats::Digits));
		if (It->GetLevel() != Gameplay->GetLoadedLevel()) { ++SourceIdentityCount; continue; }
		DestinationActorsById.Add(Id, *It); Discovered.Add(*It);
	}
	Report += FString::Printf(TEXT("SourceManifestCount=%d DestinationManifestCount=%d\n"), SourceIdentityCount, Discovered.Num());
	if (MovedCount != ExpectedRootCount || SourceIdentityCount != 0 || Discovered.Num() != ExpectedRootCount
		|| MigrationSourceKeys.Num() != ExpectedRootCount || (Moved.Num() != 0 && Moved.Num() != ExpectedRootCount)) return Finish(1, TEXT("MoveFailed"));
	for (const auto& Pair : MigrationSourceKeys)
		if (!DestinationActorsById.Contains(Pair.Key)) return Finish(1, TEXT("MigrationIdMissing:" ) + Pair.Key.ToString(EGuidFormats::Digits));
	if (!Moved.IsEmpty())
		for (AActor* Actor : Moved)
			if (!Discovered.Contains(Actor)) return Finish(1, TEXT("OutActorsSetMismatch"));
	Roots = Discovered;
	FLogicalActorKeys DestinationLogicalKeys = SourceLogicalKeys;
	for (AActor* Root : Roots)
	{
		FGuid Id;
		for (const FName Tag : Root->Tags)
		{
			const FString TagValue = Tag.ToString();
			if (TagValue.StartsWith(TEXT("SWMigrationId=")))
			{
				if (Id.IsValid() || !FGuid::Parse(TagValue.Mid(14), Id)) return Finish(1, TEXT("MovedRootIdentityInvalid"));
			}
		}
		if (!Id.IsValid()) return Finish(1, TEXT("MovedRootIdentityMissing"));
		const FString* SourceKey = MigrationSourceKeys.Find(Id);
		if (!SourceKey) return Finish(1, TEXT("MovedRootIdentityUnknown"));
		DestinationLogicalKeys.Add(Root, *SourceKey);
		const FActorBefore* Old = Before.Find(*SourceKey);
		if (!Old) return Finish(1, TEXT("MovedRootLogicalKeyMissing"));
		Root->Tags = Old->Tags;
		if (USWRoomSnapshotComponent* Snapshot = Root->FindComponentByClass<USWRoomSnapshotComponent>())
		{
			const FGuid StableActorId = Old->StableId.IsValid() ? Old->StableId : FGuid::NewGuid(); Snapshot->SetLevelInstanceId(StableActorId);
			Report += FString::Printf(TEXT("RootId Actor=%s Id=%s New=%d\n"), *Root->GetName(), *StableActorId.ToString(EGuidFormats::Digits), !Old->StableId.IsValid());
		}
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (IsHelper(*It)) continue;
		if (It->GetParentActor())
			if (USWRoomSnapshotComponent* Snapshot = It->FindComponentByClass<USWRoomSnapshotComponent>())
			{
				if (!Snapshot->RefreshLevelInstanceId()) return Finish(1, TEXT("ChildIdInvalid:") + It->GetName());
				Snapshot->SetLevelInstanceId(Snapshot->StableId);
			}
		bool bHasLifetime = false;
		for (FName Tag : It->Tags) bHasLifetime |= IsVoyageLifetimeTag(Tag.ToString());
		if (!bHasLifetime) It->Tags.AddUnique(It->GetLevel() == Gameplay->GetLoadedLevel() ? FName(TEXT("SWVoyage.Voyage")) : FName(TEXT("SWVoyage.Environment")));
	}
	TSet<FGuid> Ids;
	const FPathAliases NewPaths = WorldPaths(World, DestinationLogicalKeys);
	TMap<FString, UObject*> ReferenceTargets;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		// Helper/entry actors are retained, but may still be referenced by
		// authored gameplay properties. Index them without moving or editing.
		if (!IsValid(*It)) continue;
		ReferenceTargets.Add(WorldReferenceKey(*It, DestinationLogicalKeys), *It);
		// Index the actor's current component collection. Editor reconstruction
		// can leave retired inner objects alive until GC; they are not targets.
		TInlineComponentArray<UActorComponent*> Components(*It);
		for (UActorComponent* Object : Components)
			if (IsValid(Object) && !Object->IsTemplate())
			{
				const FString Key = WorldReferenceKey(Object, DestinationLogicalKeys);
				if (UObject* const* Existing = ReferenceTargets.Find(Key); Existing && *Existing != Object)
					return Finish(1, TEXT("MigrationWorldReferenceTargetAmbiguous:") + Key);
				ReferenceTargets.Add(Key, Object);
			}
	}
	for (const auto& Pair : BeforeActorSubobjects)
	{
		UObject* Original = Pair.Value.Object.Get();
		UObject* const* Owner = ReferenceTargets.Find(Pair.Value.ActorKey);
		if (!IsValid(Original) || !Owner || !IsValid(*Owner))
			return Finish(1, TEXT("MigrationReferencedSubobjectOwnerMissing:") + Pair.Key);
		UObject* Preserved = FindObjectFast<UObject>(*Owner, Original->GetFName());
		if (Preserved && WorldReferenceKey(Preserved, DestinationLogicalKeys) != Pair.Key)
			return Finish(1, TEXT("MigrationReferencedSubobjectNameCollision:") + Pair.Key);
		if (!Preserved) Preserved = DuplicateObject<UObject>(Original, *Owner, Original->GetFName());
		if (!IsValid(Preserved) || WorldReferenceKey(Preserved, DestinationLogicalKeys) != Pair.Key)
			return Finish(1, TEXT("MigrationReferencedSubobjectCopyFailed:") + Pair.Key);
		TMap<FString, FString> PreservedProperties;
		CaptureProperties(Preserved, NewPaths, DestinationLogicalKeys, PreservedProperties);
		if (!Pair.Value.Properties.OrderIndependentCompareEqual(PreservedProperties))
		{
			for (const auto& Property : Pair.Value.Properties)
				if (const FString* Actual = PreservedProperties.Find(Property.Key); !Actual || *Actual != Property.Value)
					Report += TEXT("ReferencedSubobjectPropertyDifference Field=") + Property.Key + TEXT(" Before=") + Property.Value
						+ TEXT(" After=") + (Actual ? *Actual : TEXT("<missing>")) + TEXT("\n");
			return Finish(1, TEXT("MigrationReferencedSubobjectPropertyMismatch:") + Pair.Key);
		}
		ReferenceTargets.Add(Pair.Key, Preserved);
		Report += TEXT("PreservedReferencedActorSubobject=") + Pair.Key + TEXT("\n");
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (IsHelper(*It)) continue;
		if (!ProcessHardReferences(*It, DestinationLogicalKeys, BeforeWorldReferences, &ReferenceTargets, Error, Report)) return Finish(1, Error);
		TInlineComponentArray<UActorComponent*> Components(*It);
		for (UActorComponent* Component : Components)
			if (Component && !Component->IsEditorOnly()
				&& !ProcessHardReferences(Component, DestinationLogicalKeys, BeforeWorldReferences, &ReferenceTargets, Error, Report)) return Finish(1, Error);
	}
	FSoftReferenceMap AfterSoftReferences;
	TSet<FString> ComparedLogicalActors;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (IsHelper(*It)) continue;
		const FActorBefore* CollisionBefore = Before.Find(ObjectKey(*It, DestinationLogicalKeys));
		if (!CollisionBefore) return Finish(1, TEXT("ActorSemanticRecordMissing:") + ObjectKey(*It, DestinationLogicalKeys));
		if (It->SpawnCollisionHandlingMethod != CollisionBefore->SpawnCollisionHandling)
		{
			Report += FString::Printf(TEXT("PreservedActorSpawnCollisionHandling Key=%s Before=%d AfterMove=%d\n"),
				*ObjectKey(*It, DestinationLogicalKeys), static_cast<int32>(CollisionBefore->SpawnCollisionHandling),
				static_cast<int32>(It->SpawnCollisionHandlingMethod));
			It->SpawnCollisionHandlingMethod = CollisionBefore->SpawnCollisionHandling;
		}
		TInlineComponentArray<UPrimitiveComponent*> Primitives(*It);
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			if (!Primitive || Primitive->IsEditorOnly()) continue;
			const FString ComponentKey = ObjectKey(Primitive, DestinationLogicalKeys);
			const ECollisionEnabled::Type* Previous = CollisionBefore->ComponentCollisionModes.Find(ComponentKey);
			if (!Previous) return Finish(1, TEXT("PrimitiveSemanticRecordMissing:") + ComponentKey);
			const ECollisionEnabled::Type Current = Primitive->BodyInstance.GetCollisionEnabled(false);
			if (Current != *Previous)
			{
				const TSharedPtr<FBodyInstance>* CollisionProperties = CollisionBefore->ComponentCollisionProperties.Find(ComponentKey);
				if (!CollisionProperties || !CollisionProperties->IsValid()) return Finish(1, TEXT("PrimitiveCollisionPropertiesNotReady:") + ComponentKey);
				Primitive->SetCollisionEnabled(*Previous);
				// The setter invalidates the profile name. Restore the captured native
				// collision settings together, preserving authored response overrides.
				Primitive->BodyInstance.CopyRuntimeBodyInstancePropertiesFrom(CollisionProperties->Get());
				Report += FString::Printf(TEXT("PreservedComponentCollision Key=%s Before=%d AfterMove=%d Restored=%d\n"),
					*ComponentKey, static_cast<int32>(*Previous), static_cast<int32>(Current),
					static_cast<int32>(Primitive->BodyInstance.GetCollisionEnabled(false)));
				if (Primitive->BodyInstance.GetCollisionEnabled(false) != *Previous)
					return Finish(1, TEXT("PrimitiveCollisionPreservationFailed:") + ComponentKey);
			}
		}
		FActorBefore After; CaptureActor(*It, NewPaths, DestinationLogicalKeys, After);
		CaptureSoftReferences(*It, DestinationLogicalKeys, AfterSoftReferences);
		TInlineComponentArray<UActorComponent*> Components(*It);
		for (UActorComponent* Component : Components)
			if (Component && !Component->IsEditorOnly()) CaptureSoftReferences(Component, DestinationLogicalKeys, AfterSoftReferences);
		const FActorBefore* Old = Before.Find(ObjectKey(*It, DestinationLogicalKeys));
		const FString LogicalActorKey = ObjectKey(*It, DestinationLogicalKeys);
		if (!Old) return Finish(1, TEXT("ActorSemanticRecordMissing:") + LogicalActorKey);
		if (Old->Class != After.Class || !Old->Transform.Equals(After.Transform) || Old->ParentKey != After.ParentKey
			|| Old->ComponentClasses.Num() != After.ComponentClasses.Num()
			|| !TagsMatchAfterMigration(Old->Tags, After.Tags, !Old->StableId.IsValid() && After.StableId.IsValid())
			|| (Old->StableId.IsValid() && Old->StableId != After.StableId))
		{
			Report += FString::Printf(TEXT("ActorDifference Key=%s ClassBefore=%s ClassAfter=%s TransformBefore=%s TransformAfter=%s ParentBefore=%s ParentAfter=%s ComponentCountBefore=%d ComponentCountAfter=%d TagsBefore=%s TagsAfter=%s StableIdBefore=%s StableIdAfter=%s\n"),
				*LogicalActorKey, *Old->Class, *After.Class, *Old->Transform.ToHumanReadableString(), *After.Transform.ToHumanReadableString(),
				*Old->ParentKey, *After.ParentKey, Old->ComponentClasses.Num(), After.ComponentClasses.Num(),
				*TagsToString(Old->Tags), *TagsToString(After.Tags), *Old->StableId.ToString(EGuidFormats::Digits), *After.StableId.ToString(EGuidFormats::Digits));
			return Finish(1, TEXT("ActorSemanticMismatch:") + LogicalActorKey);
		}
		if (ComparedLogicalActors.Contains(LogicalActorKey)) return Finish(1, TEXT("PostLogicalActorDuplicate:") + LogicalActorKey);
		ComparedLogicalActors.Add(LogicalActorKey);
		for (const auto& ComponentPair : Old->ComponentClasses)
		{
			const FString* NewComponentClass = After.ComponentClasses.Find(ComponentPair.Key);
			if (!NewComponentClass || *NewComponentClass != ComponentPair.Value)
			{
				Report += FString::Printf(TEXT("ComponentDifference Key=%s Before=%s After=%s\n"), *ComponentPair.Key, *ComponentPair.Value, NewComponentClass ? **NewComponentClass : TEXT("Missing"));
				return Finish(1, TEXT("ComponentSemanticMismatch:") + ComponentPair.Key);
			}
		}
		for (const auto& Property : Old->Properties)
		{
			const FString* Value = After.Properties.Find(Property.Key);
			if (!Value || *Value != Property.Value)
			{
				Report += FString::Printf(TEXT("PropertyDifference Field=%s Before=%s After=%s\n"), *Property.Key, *Property.Value, Value ? **Value : TEXT("Missing"));
				const FString CollisionKey = Property.Key.Left(Property.Key.Find(TEXT("|"))) + TEXT("|BodyInstanceCollisionEnabledValue");
				const FString* OldCollision = Old->Properties.Find(CollisionKey);
				const FString* NewCollision = After.Properties.Find(CollisionKey);
				if (OldCollision || NewCollision)
					Report += FString::Printf(TEXT("BodyCollisionValue Before=%s After=%s\n"),
						OldCollision ? **OldCollision : TEXT("Missing"), NewCollision ? **NewCollision : TEXT("Missing"));
				return Finish(1, TEXT("ReflectedPropertyMismatch"));
			}
		}
		if (USWRoomSnapshotComponent* Snapshot = It->FindComponentByClass<USWRoomSnapshotComponent>(); Snapshot && Snapshot->PersistenceClass != ESWRoomPersistenceClass::Transient)
		{
			if (!Snapshot->StableId.IsValid() || Ids.Contains(Snapshot->StableId)) return Finish(1, TEXT("StableIdDuplicateOrMissing"));
			Ids.Add(Snapshot->StableId);
		}
	}
	if (ComparedLogicalActors.Num() != Before.Num()) return Finish(1, TEXT("PostActorSetCountMismatch"));
	for (TActorIterator<AActor> It(World); It; ++It)
		for (FName Tag : It->Tags)
			if (Tag.ToString().StartsWith(TEXT("SWMigrationId="))) return Finish(1, TEXT("MigrationIdTagRetained:") + It->GetPathName());
	FString SoftReferenceDifference;
	if (!CompareSoftReferenceMaps(BeforeSoftReferences, AfterSoftReferences, SoftReferenceDifference))
	{
		Report += TEXT("SoftReferenceDifference=") + SoftReferenceDifference + TEXT("\n");
		return Finish(1, TEXT("SoftReferenceAuditFailed"));
	}
	if (HasCrossLevelReferences(World, Error)) return Finish(1, Error);
	if (!ValidateIds(World, true, Error)) return Finish(1, Error);
	TSet<TWeakObjectPtr<AActor>> PostChildSet;
	TSet<TWeakObjectPtr<AActor>> PostChildVisiting;
	for (AActor* Root : Roots)
		if (!CollectChildActorsRecursive(Root, PostChildSet, PostChildVisiting)) return Finish(1, TEXT("PostChildActorRelationshipInvalid"));
	if (PostChildSet.Num() != ExpectedChildCount) return Finish(1, TEXT("MovedChildCountInvalid"));
	for (const TWeakObjectPtr<AActor>& Child : PostChildSet)
		if (!Child.IsValid() || Child->GetLevel() != Gameplay->GetLoadedLevel()) return Finish(1, TEXT("MovedChildLevelMismatch"));
	UPackage* ProfilePackage = CreatePackage(*ProfileAsset);
	USWVoyageResetProfile* Profile = NewObject<USWVoyageResetProfile>(ProfilePackage, *FPackageName::GetShortName(ProfileAsset), RF_Public | RF_Standalone);
	Profile->GameplayLevel = FSoftObjectPath(GameplayMap + TEXT(".") + FPackageName::GetShortName(GameplayMap));
	Profile->ProjectScriptPackages = ScriptPackages;
	FActorSpawnParameters Spawn; Spawn.Name = TEXT("SWVoyageResetAnchor"); Spawn.OverrideLevel = World->PersistentLevel;
	ASWVoyageResetAnchor* Anchor = World->SpawnActor<ASWVoyageResetAnchor>(ASWVoyageResetAnchor::StaticClass(), FTransform::Identity, Spawn);
	if (!Anchor) return Finish(1, TEXT("AnchorCreationFailed"));
	Anchor->Profile = Profile; FAssetRegistryModule::AssetCreated(Profile);
	World->MarkPackageDirty(); Gameplay->GetLoadedLevel()->GetOutermost()->MarkPackageDirty(); ProfilePackage->MarkPackageDirty();
	const TSet<FString> AllowedDirtyPackages = { SourceMap, GameplayMap, ProfileAsset };
	for (TObjectIterator<UPackage> It; It; ++It)
		if (It->IsDirty() && !DirtyPackagesBefore.Contains(It->GetName()) && !AllowedDirtyPackages.Contains(It->GetName()))
			return Finish(1, TEXT("UnexpectedDirtyPackage:") + It->GetName());
	const TArray<UPackage*> Packages = { Gameplay->GetLoadedLevel()->GetOutermost(), ProfilePackage, World->GetOutermost() };
	const TArray<FString> Files = { GameplayFile, ProfileFile, PersistentFile };
	const TArray<UObject*> SaveObjects = { static_cast<UObject*>(UWorld::FindWorldInPackage(Packages[0])), static_cast<UObject*>(Profile), static_cast<UObject*>(World) };
	bool bSaved = true;
	for (int32 Index = 0; Index < Packages.Num(); ++Index)
	{
		FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
		const bool bResult = UPackage::SavePackage(Packages[Index], SaveObjects[Index], *Files[Index], Args);
		FString Hash;
		const bool bHasHash = IFileManager::Get().FileExists(*Files[Index]) && FileHash(Files[Index], Hash, HashError);
		bSaved &= bResult && bHasHash;
		Report += FString::Printf(TEXT("FinalSave File=%s Success=%d Hash=%s HashError=%s\n"), *Files[Index], bResult, *Hash, bHasHash ? TEXT("") : *HashError);
		if (!bResult || !bHasHash)
		{
			for (int32 Remaining = Index + 1; Remaining < Packages.Num(); ++Remaining)
				Report += TEXT("FinalSave NotAttempted File=") + Files[Remaining] + TEXT("\n");
			return Finish(1, TEXT("SaveFailed; see package/hash report and backups"));
		}
	}
	return Finish(bSaved ? 0 : 1, bSaved ? TEXT("AppliedValidated SavedPackages=3") : TEXT("SaveFailed; see package/hash report"));
}
