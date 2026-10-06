#include "Room/SWRoomSnapshotComponent.h"
#include "GameFramework/Actor.h"
#if WITH_EDITOR
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#endif

namespace
{
const FString IdTagPrefix = TEXT("SWRoomStableId=");
}

USWRoomSnapshotComponent::USWRoomSnapshotComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool USWRoomSnapshotComponent::RefreshLevelInstanceId()
{
	const AActor* Owner = GetOwner();
	if (!Owner) return false;
	FGuid FoundId;
	int32 Count = 0;
	for (const FName& Tag : Owner->Tags)
	{
		const FString Value = Tag.ToString();
		if (!Value.StartsWith(IdTagPrefix)) continue;
		++Count;
		if (!FGuid::Parse(Value.RightChop(IdTagPrefix.Len()), FoundId)) return false;
	}
	if (Count != 1 || !FoundId.IsValid()) return false;
	StableId = FoundId;
	return true;
}

void USWRoomSnapshotComponent::SetLevelInstanceId(const FGuid& Id)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Id.IsValid()) return;
	Owner->Tags.RemoveAll([](const FName& Tag) { return Tag.ToString().StartsWith(IdTagPrefix); });
	Owner->Tags.Add(FName(*(IdTagPrefix + Id.ToString(EGuidFormats::Digits))));
	StableId = Id;
}

void USWRoomSnapshotComponent::OnRegister()
{
	Super::OnRegister();
	if (GetOwner() && GetOwner()->HasAnyFlags(RF_WasLoaded)) RefreshLevelInstanceId();
}

#if WITH_EDITOR
void USWRoomSnapshotComponent::OnComponentCreated()
{
	Super::OnComponentCreated();
	if (GetOwner() && !GetOwner()->HasAnyFlags(RF_WasLoaded) && !IsTemplate() && !StableId.IsValid())
		SetLevelInstanceId(FGuid::NewGuid());
}

void USWRoomSnapshotComponent::PostEditImport()
{
	Super::PostEditImport();
	QueueEditorDuplicateId();
}

void USWRoomSnapshotComponent::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	if (DuplicateMode != EDuplicateMode::PIE) QueueEditorDuplicateId();
}

void USWRoomSnapshotComponent::QueueEditorDuplicateId()
{
	if (IsTemplate() || bPendingEditorDuplicateId) return;
	bPendingEditorDuplicateId = true;
	EditorDuplicateIdAttempts = 0;
	const TWeakObjectPtr<USWRoomSnapshotComponent> WeakThis(this);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float DeltaTime)
	{
		return WeakThis.IsValid() ? WeakThis->ApplyPendingEditorDuplicateId(DeltaTime) : false;
	}));
}

bool USWRoomSnapshotComponent::ApplyPendingEditorDuplicateId(float DeltaTime)
{
	(void)DeltaTime;
	AActor* Owner = GetOwner();
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	if (!Owner || !World || !Owner->GetLevel())
	{
		if (++EditorDuplicateIdAttempts < 3) return true;
		UE_LOG(LogTemp, Warning, TEXT("Room ID assignment deferred actor registration did not complete: %s"), *GetPathName());
		bPendingEditorDuplicateId = false;
		return false;
	}
	bPendingEditorDuplicateId = false;
	if (IsTemplate() || World->WorldType != EWorldType::Editor) return false;

	TSet<FGuid> UsedIds;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Other = *It;
		if (!Other || Other == Owner) continue;
		const USWRoomSnapshotComponent* OtherSnapshot = Other->FindComponentByClass<USWRoomSnapshotComponent>();
		if (!OtherSnapshot || OtherSnapshot->PersistenceClass == ESWRoomPersistenceClass::Transient) continue;
		for (const FName& Tag : Other->Tags)
		{
			const FString Value = Tag.ToString();
			FGuid Id;
			if (Value.StartsWith(IdTagPrefix) && FGuid::Parse(Value.RightChop(IdTagPrefix.Len()), Id) && Id.IsValid())
				UsedIds.Add(Id);
		}
	}
	FGuid NewId;
	do { NewId = FGuid::NewGuid(); } while (!NewId.IsValid() || UsedIds.Contains(NewId));
	const FGuid OldId = StableId;
	Owner->Modify();
	Modify();
	SetLevelInstanceId(NewId);
	Owner->MarkPackageDirty();
	UE_LOG(LogTemp, Display, TEXT("Room ID assigned to duplicated actor: Actor=%s Old=%s New=%s"),
		*Owner->GetPathName(), *OldId.ToString(), *NewId.ToString());
	return false;
}
#endif
