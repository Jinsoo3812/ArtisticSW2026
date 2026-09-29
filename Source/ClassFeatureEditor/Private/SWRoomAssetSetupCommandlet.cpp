#include "SWRoomAssetSetupCommandlet.h"

#include "Room/SWLevelEntryPoint.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomSnapshotSubsystem.h"
#include "Room/SWRoomSaveGame.h"
#include "Room/SWRoomSaveStore.h"
#include "UI/SWRoomMenuWidget.h"
#include "KelvinShip.h"
#include "PlayerRespawnPointComponent.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameMapsSettings.h"
#include "Settings/ProjectPackagingSettings.h"
#include "Engine/LevelStreaming.h"
#include "Engine/Level.h"
#include "GameFramework/PlayerStart.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"

int32 USWRoomAssetSetupCommandlet::Main(const FString& Params)
{
	if (Params.Contains(TEXT("AssignAllRoomStableIds")) || Params.Contains(TEXT("VerifyAllRoomStableIds")))
	{
		const bool bAssign = Params.Contains(TEXT("AssignAllRoomStableIds"));
		TArray<FString> PendingMaps;
		auto QueueMap = [&PendingMaps](const FString& Path)
		{
			FString Package = FSoftObjectPath(Path).GetLongPackageName();
			if (Package.IsEmpty()) Package = Path;
			if (Package.StartsWith(TEXT("/Game/")))
				PendingMaps.AddUnique(Package);
		};
		TArray<FAssetData> MapAssets;
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByPath(
			FName(TEXT("/Game")), MapAssets, true);
		for (const FAssetData& Asset : MapAssets)
		{
			if (Asset.AssetClassPath == UWorld::StaticClass()->GetClassPathName()) QueueMap(Asset.PackageName.ToString());
		}
		PendingMaps.Sort();
		TMap<FString, TArray<FString>> StreamedMaps;
		TMap<FString, TArray<TPair<FString, FGuid>>> Participants;
		TSet<FGuid> UsedIds;
		for (int32 Index = 0; Index < PendingMaps.Num(); ++Index)
		{
			const FString Package = PendingMaps[Index];
			UWorld* IdWorld = UEditorLoadingAndSavingUtils::LoadMap(Package);
			if (!IdWorld)
			{
				UE_LOG(LogTemp, Error, TEXT("SWRoom ID setup: map unavailable %s"), *Package);
				return 30;
			}
			for (const ULevelStreaming* Streaming : IdWorld->GetStreamingLevels())
				if (Streaming)
				{
					const FString Child = Streaming->GetWorldAssetPackageName();
					QueueMap(Child);
					StreamedMaps.FindOrAdd(Package).AddUnique(Child);
				}
			for (AActor* Actor : IdWorld->PersistentLevel->Actors)
			{
				USWRoomSnapshotComponent* Component = Actor ? Actor->FindComponentByClass<USWRoomSnapshotComponent>() : nullptr;
				if (!Component || Component->PersistenceClass == ESWRoomPersistenceClass::Transient) continue;
				const FGuid Id = Component->RefreshLevelInstanceId() ? Component->StableId : FGuid();
				Participants.FindOrAdd(Package).Emplace(Actor->GetPathName(), Id);
				if (Id.IsValid()) UsedIds.Add(Id);
			}
		}
		TSet<FString> ReassignPaths;
		for (const FString& RootMap : PendingMaps)
		{
			TSet<FString> Group;
			TArray<FString> Queue{RootMap};
			for (int32 Index = 0; Index < Queue.Num(); ++Index)
			{
				const FString Package = Queue[Index];
				if (Group.Contains(Package)) continue;
				Group.Add(Package);
				if (const TArray<FString>* Children = StreamedMaps.Find(Package)) Queue.Append(*Children);
			}
			TMap<FGuid, TArray<FString>> PathsById;
			for (const FString& Package : Group)
				if (const TArray<TPair<FString, FGuid>>* Rows = Participants.Find(Package))
					for (const TPair<FString, FGuid>& Row : *Rows)
						if (Row.Value.IsValid()) PathsById.FindOrAdd(Row.Value).Add(Row.Key);
			for (TPair<FGuid, TArray<FString>>& Pair : PathsById)
			{
				Pair.Value.Sort();
				for (int32 Index = 1; Index < Pair.Value.Num(); ++Index) ReassignPaths.Add(Pair.Value[Index]);
			}
		}
		int32 Errors = 0;
		for (const FString& Package : PendingMaps)
		{
			UWorld* IdWorld = UEditorLoadingAndSavingUtils::LoadMap(Package);
			if (!IdWorld) { ++Errors; continue; }
			int32 Changed = 0;
			for (AActor* Actor : IdWorld->PersistentLevel->Actors)
			{
				USWRoomSnapshotComponent* Component = Actor ? Actor->FindComponentByClass<USWRoomSnapshotComponent>() : nullptr;
				if (!Component || Component->PersistenceClass == ESWRoomPersistenceClass::Transient) continue;
				const FGuid OldId = Component->RefreshLevelInstanceId() ? Component->StableId : FGuid();
				if (OldId.IsValid() && !ReassignPaths.Contains(Actor->GetPathName())) continue;
				if (!bAssign)
				{
					UE_LOG(LogTemp, Error, TEXT("SWRoom ID verify: Map=%s Actor=%s ID=%s Reason=%s"),
						*Package, *Actor->GetPathName(), *OldId.ToString(), OldId.IsValid() ? TEXT("Duplicate") : TEXT("Missing"));
					++Errors;
					continue;
				}
				FGuid NewId;
				do { NewId = FGuid::NewGuid(); } while (UsedIds.Contains(NewId));
				UsedIds.Add(NewId);
				Actor->Modify(); Component->Modify();
				Component->SetLevelInstanceId(NewId);
				Actor->MarkPackageDirty();
				UE_LOG(LogTemp, Display, TEXT("SWRoom ID assign: Map=%s Actor=%s Old=%s New=%s"),
					*Package, *Actor->GetPathName(), *OldId.ToString(), *NewId.ToString());
				++Changed;
			}
			if (Changed && !UEditorLoadingAndSavingUtils::SaveMap(IdWorld, Package))
			{
				UE_LOG(LogTemp, Error, TEXT("SWRoom ID setup: save failed Map=%s Actors=%d"), *Package, Changed);
				++Errors;
			}
			UE_LOG(LogTemp, Display, TEXT("SWRoom ID setup: Map=%s Participants=%d Changed=%d"),
				*Package, Participants.FindRef(Package).Num(), Changed);
		}
		return Errors ? 31 : 0;
	}
	const FString MapAsset = UGameMapsSettings::GetGameDefaultMap(EDefaultMapRequestType::Server);
	const FSoftObjectPath MapPath(MapAsset);
	if (MapPath.IsNull() || MapAsset.Contains(TEXT("ConnectionLobby"))) return 1;
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(MapPath.GetLongPackageName());
	if (!World) { UE_LOG(LogTemp, Error, TEXT("SWRoom setup: cannot load %s"), *MapAsset); return 2; }
	if (Params.Contains(TEXT("InspectCurrentRoom")))
	{
		const USWRoomSaveGame* Saved = FSWRoomSaveStore::LoadCurrentRoom(GetTransientPackage());
		if (!Saved) { UE_LOG(LogTemp, Error, TEXT("SWRoom inspect: current room unavailable")); return 21; }
		UE_LOG(LogTemp, Display, TEXT("SWRoom inspect: Sequence=%llu Contract=%d Guests=%d SharedShipUpgradeNodes=%d"),
			Saved->CaptureSequence, Saved->ContentContractVersion, Saved->Guests.Num(), Saved->SharedProgress.ShipUpgradeNodeIds.Num());
		for (int32 Index = 0; Index < Saved->Guests.Num(); ++Index)
			UE_LOG(LogTemp, Display, TEXT("SWRoom inspect: guest %d Resume=%d Location=%s InventorySlots=%d PersonalUpgradeNodes=%d"),
				Index, Saved->Guests[Index].Progress.bHasResumeTransform,
				*Saved->Guests[Index].Progress.ResumeWorldTransform.GetLocation().ToString(),
				Saved->Guests[Index].Progress.InventorySlots.Num(), Saved->Guests[Index].Progress.UpgradeNodeIds.Num());
		for (const FSWRoomActorRecord& Record : Saved->WorldSnapshot.Actors)
		{
			UE_LOG(LogTemp, Display, TEXT("SWRoom inspect: saved ID=%s Class=%s Origin=%s Required=%d Location=%s Partition=%s"),
				*Record.StableId.ToString(), *Record.ClassPath.ToString(), *UEnum::GetValueAsString(Record.Origin),
				Record.bRequired, *Record.WorldTransform.GetLocation().ToString(), *Record.LevelPartition.PackagePath.ToString());
		}
		for (TActorIterator<AActor> It(World); It; ++It)
			if (const USWRoomSnapshotComponent* Component = It->FindComponentByClass<USWRoomSnapshotComponent>(); Component && Component->StableId.IsValid())
				UE_LOG(LogTemp, Display, TEXT("SWRoom inspect: map ID=%s ActorGuid=%s Class=%s Name=%s Loaded=%d Location=%s"),
					*Component->StableId.ToString(), *It->GetActorGuid().ToString(), *It->GetClass()->GetPathName(), *It->GetName(), It->HasAnyFlags(RF_WasLoaded),
					*It->GetActorLocation().ToString());
		return 0;
	}
	if (Params.Contains(TEXT("PreviewRepairCurrentRoomIds")) || Params.Contains(TEXT("RepairCurrentRoomIds")))
	{
		const bool bApply = Params.Contains(TEXT("RepairCurrentRoomIds")) && !Params.Contains(TEXT("PreviewRepairCurrentRoomIds"));
		USWRoomSaveGame* Saved = FSWRoomSaveStore::LoadCurrentRoom(GetTransientPackage());
		if (!Saved || Saved->bRecoveredFromBackup || Saved->ContentContractVersion != 1
			|| Saved->MapPath.GetLongPackageName() != MapPath.GetLongPackageName() || !Saved->WorldSnapshot.DestroyedLevelActorIds.IsEmpty()
			|| !Saved->WorldSnapshot.UnloadedActors.IsEmpty())
		{
			UE_LOG(LogTemp, Error, TEXT("SWRoom ID repair: outside legacy contract HasSave=%d Backup=%d Version=%d SavedMap=%s ConfigMap=%s Tombstones=%d Unloaded=%d"),
				Saved != nullptr, Saved && Saved->bRecoveredFromBackup, Saved ? Saved->ContentContractVersion : -1,
				Saved ? *Saved->MapPath.ToString() : TEXT("None"), *MapPath.ToString(), Saved ? Saved->WorldSnapshot.DestroyedLevelActorIds.Num() : -1,
				Saved ? Saved->WorldSnapshot.UnloadedActors.Num() : -1);
			return 22;
		}
		TMap<FGuid, FGuid> RemappedIds;
		TSet<FGuid> UsedMapIds;
		for (FSWRoomActorRecord& Record : Saved->WorldSnapshot.Actors)
		{
			if (Record.Origin != ESWRoomSpawnOrigin::LevelPlaced) continue;
			AActor* Best = nullptr;
			FGuid BestId;
			double BestDistance = TNumericLimits<double>::Max();
			double SecondDistance = TNumericLimits<double>::Max();
			int32 CandidateCount = 0;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				USWRoomSnapshotComponent* Component = It->FindComponentByClass<USWRoomSnapshotComponent>();
				if (!Component || !Component->RefreshLevelInstanceId() || UsedMapIds.Contains(Component->StableId)
					|| FSoftClassPath(It->GetClass()) != Record.ClassPath
					|| FSoftObjectPath(It->GetLevel()->GetOutermost()->GetName()) != Record.LevelPartition.PackagePath) continue;
				++CandidateCount;
				const double Distance = FVector::DistSquared(Record.WorldTransform.GetLocation(), It->GetActorLocation());
				if (Distance < BestDistance)
				{
					SecondDistance = BestDistance;
					BestDistance = Distance;
					Best = *It;
					BestId = Component->StableId;
				}
				else SecondDistance = FMath::Min(SecondDistance, Distance);
			}
			if (!Best || (CandidateCount > 1 && (BestDistance > FMath::Square(1000.0)
				|| SecondDistance - BestDistance < FMath::Square(500.0))))
			{
				UE_LOG(LogTemp, Error, TEXT("SWRoom ID repair: ambiguous actor %s Class=%s Candidates=%d BestDistanceCm=%.1f"),
					*Record.StableId.ToString(), *Record.ClassPath.ToString(), CandidateCount, FMath::Sqrt(BestDistance));
				return 23;
			}
			UE_LOG(LogTemp, Display, TEXT("SWRoom ID repair: %s -> %s Class=%s DistanceCm=%.1f"),
				*Record.StableId.ToString(), *BestId.ToString(), *Record.ClassPath.ToString(), FMath::Sqrt(BestDistance));
			UsedMapIds.Add(BestId);
			RemappedIds.Add(Record.StableId, BestId);
		}
		if (RemappedIds.Num() != Saved->WorldSnapshot.Actors.Num())
		{ UE_LOG(LogTemp, Error, TEXT("SWRoom ID repair: this tool requires all saved actors to be level placed")); return 24; }
		auto ReplaceId = [&RemappedIds](FGuid& Id)
		{
			if (const FGuid* NewId = RemappedIds.Find(Id)) Id = *NewId;
		};
		for (FSWRoomActorRecord& Record : Saved->WorldSnapshot.Actors)
		{
			ReplaceId(Record.StableId);
			ReplaceId(Record.CreatorId);
			for (FGuid& Id : Record.ReferenceIds) ReplaceId(Id);
		}
		for (FGuid& Id : Saved->WorldSnapshot.ReferenceIds) ReplaceId(Id);
		ReplaceId(Saved->HostProgress.ShipStableId);
		ReplaceId(Saved->HostProgress.MountedDeviceId);
		for (FSWRoomGuestProgress& Guest : Saved->Guests)
		{
			ReplaceId(Guest.Progress.ShipStableId);
			ReplaceId(Guest.Progress.MountedDeviceId);
		}
		Saved->ContentContractVersion = USWRoomSaveGame::CurrentContentContractVersion;
		if (!FSWRoomSaveStore::Validate(Saved))
		{ UE_LOG(LogTemp, Error, TEXT("SWRoom ID repair: remapped room failed validation")); return 25; }
		if (!bApply)
		{ UE_LOG(LogTemp, Display, TEXT("SWRoom ID repair preview: %d unique mappings validated; save unchanged"), RemappedIds.Num()); return 0; }
		IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
		const FString CurrentPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SWRoom"), TEXT("CurrentRoom.sav"));
		const FString ArchivePath = FPaths::Combine(FPaths::GetPath(CurrentPath), FString::Printf(
			TEXT("CurrentRoom.pre-id-repair.%lld.sav"), FDateTime::UtcNow().ToUnixTimestamp()));
		if (Files.FileExists(*ArchivePath) || !Files.CopyFile(*ArchivePath, *CurrentPath))
		{ UE_LOG(LogTemp, Error, TEXT("SWRoom ID repair: original save archive failed")); return 26; }
		if (!FSWRoomSaveStore::WriteCurrentRoom(Saved))
		{ UE_LOG(LogTemp, Error, TEXT("SWRoom ID repair: file transaction failed; archive=%s"), *ArchivePath); return 27; }
		UE_LOG(LogTemp, Display, TEXT("SWRoom ID repair: committed %d mappings; original=%s"), RemappedIds.Num(), *ArchivePath);
		return 0;
	}
	UE_LOG(LogTemp, Display, TEXT("SWRoom setup: configured map has %d streaming levels"), World->GetStreamingLevels().Num());

	APlayerStart* PlayerStarts[2] = {nullptr, nullptr};
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		if (It->ActorHasTag(TEXT("Player_0"))) PlayerStarts[0] = *It;
		if (It->ActorHasTag(TEXT("Player_1"))) PlayerStarts[1] = *It;
	}
	AKelvinShip* Ship = nullptr;
	for (TActorIterator<AKelvinShip> It(World); It; ++It)
	{
		if (Ship) { UE_LOG(LogTemp, Error, TEXT("SWRoom setup: multiple player ships")); return 3; }
		Ship = *It;
	}
	if (!Ship)
	{
		UE_LOG(LogTemp, Error, TEXT("SWRoom setup: KelvinShip entry source missing"));
		return 4;
	}
	const UPlayerRespawnPointComponent* RespawnPoints[2] = {Ship->Player0RespawnPoint, Ship->Player1RespawnPoint};
	if ((!PlayerStarts[0] && !RespawnPoints[0]) || (!PlayerStarts[1] && !RespawnPoints[1]))
	{
		UE_LOG(LogTemp, Error, TEXT("SWRoom setup: safe player entry source missing"));
		return 4;
	}
	const FTransform Sources[3] = {
		PlayerStarts[0] ? PlayerStarts[0]->GetActorTransform() : RespawnPoints[0]->GetComponentTransform(),
		PlayerStarts[1] ? PlayerStarts[1]->GetActorTransform() : RespawnPoints[1]->GetComponentTransform(),
		Ship->GetActorTransform()
	};
	int32 Counts[3] = {0, 0, 0};
	for (TActorIterator<ASWLevelEntryPoint> It(World); It; ++It) ++Counts[static_cast<int32>(It->EntryRole)];
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (Counts[Index] > 1) { UE_LOG(LogTemp, Error, TEXT("SWRoom setup: duplicate entry role %d"), Index); return 5; }
		if (Counts[Index] == 0)
		{
			ASWLevelEntryPoint* Entry = World->SpawnActor<ASWLevelEntryPoint>(ASWLevelEntryPoint::StaticClass(), Sources[Index]);
			if (!Entry) return 6;
			Entry->Modify();
			Entry->EntryRole = static_cast<ESWLevelEntryRole>(Index);
			UE_LOG(LogTemp, Display, TEXT("SWRoom setup: placed entry %d at source transform"), Index);
		}
	}
	TSet<FGuid> Seen;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		USWRoomSnapshotComponent* Component = It->FindComponentByClass<USWRoomSnapshotComponent>();
		if (!Component || Component->PersistenceClass == ESWRoomPersistenceClass::Transient) continue;
		if (!Component->RefreshLevelInstanceId() || Seen.Contains(Component->StableId))
		{
			It->Modify();
			Component->Modify();
			Component->SetLevelInstanceId(FGuid::NewGuid());
			Component->MarkPackageDirty();
			It->MarkPackageDirty();
			UE_LOG(LogTemp, Warning, TEXT("SWRoom setup: assigned new ID to %s"), *It->GetPathName());
		}
		Seen.Add(Component->StableId);
	}
	UE_LOG(LogTemp, Display, TEXT("SWRoom setup: verified %d unique persistent actor IDs and three entry roles"), Seen.Num());
	if (!UEditorLoadingAndSavingUtils::SaveMap(World, MapPath.GetLongPackageName())) return 7;

	UInputAction* Action = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_RoomMenu.IA_RoomMenu"));
	if (!Action)
	{
		UPackage* Package = CreatePackage(TEXT("/Game/Input/Actions/IA_RoomMenu"));
		Action = NewObject<UInputAction>(Package, TEXT("IA_RoomMenu"), RF_Public | RF_Standalone);
		if (!Action) return 8;
		Action->ValueType = EInputActionValueType::Boolean;
		FAssetRegistryModule::AssetCreated(Action);
		Package->MarkPackageDirty();
		if (!UEditorLoadingAndSavingUtils::SavePackages({Package}, true)) return 9;
	}
	UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default"));
	if (!Context) return 10;
	bool bMapped = false;
	for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		if (Mapping.Action == Action && Mapping.Key == EKeys::Escape) bMapped = true;
	if (!bMapped)
	{
		Context->Modify();
		Context->MapKey(Action, EKeys::Escape);
		Context->MarkPackageDirty();
		if (!UEditorLoadingAndSavingUtils::SavePackages({Context->GetOutermost()}, true)) return 11;
	}
	if (!LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/UI/Room/WBP_RoomMenu.WBP_RoomMenu")))
	{
		UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
		Factory->ParentClass = USWRoomMenuWidget::StaticClass();
		IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UObject* Widget = Tools.CreateAsset(TEXT("WBP_RoomMenu"), TEXT("/Game/UI/Room"), UWidgetBlueprint::StaticClass(), Factory);
		if (!Widget || !UEditorLoadingAndSavingUtils::SavePackages({Widget->GetOutermost()}, true)) return 12;
	}
	if (!LoadClass<USWRoomMenuWidget>(nullptr, TEXT("/Game/UI/Room/WBP_RoomMenu.WBP_RoomMenu_C"))) return 13;
	if (Params.Contains(TEXT("TestSnapshot")))
	{
		const FString Pending = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SWRoom"), TEXT("CurrentRoom.v4.pending.sav"));
		if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*Pending)) return 14;
		USWRoomSnapshotSubsystem* Snapshot = World->GetSubsystem<USWRoomSnapshotSubsystem>();
		FSWRoomWorldSnapshot Data;
		FString Error;
		if (!Snapshot || !Snapshot->Capture(Data, ESWRoomSaveKind::New, 1, Error))
		{ UE_LOG(LogTemp, Error, TEXT("SWRoom snapshot smoke capture failed: %s"), *Error); return 15; }
		USWRoomSaveGame* Room = NewObject<USWRoomSaveGame>(GetTransientPackage());
		Room->RoomId = FGuid::NewGuid();
		Room->ContentContractVersion = USWRoomSaveGame::CurrentContentContractVersion;
		Room->HostDisplayName = TEXT("SnapshotTest");
		Room->MapPath = Data.MapPath;
		Room->WorldSnapshot = MoveTemp(Data);
		Room->CaptureSequence = 1;
		Room->SavedAtUtc = FDateTime::UtcNow();
		Room->bComplete = true;
		if (!FSWRoomSaveStore::Validate(Room) || !FSWRoomSaveStore::StageCompleteNewRoom(Room)) return 16;
		USWRoomSaveGame* RoundTrip = FSWRoomSaveStore::LoadStagedNewRoom(GetTransientPackage());
		const bool bValid = FSWRoomSaveStore::Validate(RoundTrip) && RoundTrip->WorldSnapshot.Actors.Num() == Room->WorldSnapshot.Actors.Num();
		FSWRoomSaveStore::DiscardStagedNewRoom();
		if (!bValid) return 17;
		TArray<FString> Differences;
		if (!USWRoomSnapshotSubsystem::CompareDeclared(Room->WorldSnapshot, RoundTrip->WorldSnapshot, Differences))
		{ UE_LOG(LogTemp, Error, TEXT("SWRoom disk round-trip differences: %s"), *FString::Join(Differences, TEXT(", "))); return 18; }
		AActor* MovedActor = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			const USWRoomSnapshotComponent* Component = It->FindComponentByClass<USWRoomSnapshotComponent>();
			if (Component && Component->StableId.IsValid()) { MovedActor = *It; break; }
		}
		if (!MovedActor) return 19;
		const FTransform OriginalTransform = MovedActor->GetActorTransform();
		MovedActor->SetActorLocation(OriginalTransform.GetLocation() + FVector(123.0, 0.0, 0.0));
		if (!Snapshot->Restore(RoundTrip->WorldSnapshot, Error))
		{ UE_LOG(LogTemp, Error, TEXT("SWRoom live restore failed: %s"), *Error); return 20; }
		if (!MovedActor->GetActorTransform().Equals(OriginalTransform, 0.01f)) return 21;
		FSWRoomWorldSnapshot Restored;
		if (!Snapshot->Capture(Restored, ESWRoomSaveKind::New, 1, Error)
			|| !USWRoomSnapshotSubsystem::CompareDeclared(RoundTrip->WorldSnapshot, Restored, Differences))
		{ UE_LOG(LogTemp, Error, TEXT("SWRoom live restore differences: %s / %s"), *Error, *FString::Join(Differences, TEXT(", "))); return 22; }
		UE_LOG(LogTemp, Display, TEXT("SWRoom snapshot disk and live restore comparison passed for %d registered actors"), Room->WorldSnapshot.Actors.Num());
	}
	return 0;
}
