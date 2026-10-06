#include "Room/SWRoomSnapshotComponent.h"
#include "GameFramework/Actor.h"
#include "Components/ChildActorComponent.h"
#include "Misc/SecureHash.h"
#if WITH_EDITOR
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#endif

namespace
{
const FString IdTagPrefix = TEXT("SWRoomStableId=");
#if WITH_EDITOR
int32 EditorVoyageMigrationDepth = 0;
#endif

const UChildActorComponent* FindAuthoredParentComponent(const AActor* Actor)
{
	const AActor* Parent = Actor ? Actor->GetParentActor() : nullptr;
	if (!Parent) return nullptr;
	TInlineComponentArray<UChildActorComponent*> Components(Parent);
	for (const UChildActorComponent* Component : Components)
	{
		if (Component && Component->GetChildActor() == Actor) return Component;
	}
	return nullptr;
}

bool IsAuthored(const AActor* Actor, TSet<const AActor*>& Visited)
{
	if (!Actor || Visited.Contains(Actor)) return false;
	Visited.Add(Actor);
	if (const UChildActorComponent* ParentComponent = FindAuthoredParentComponent(Actor))
		return IsAuthored(ParentComponent->GetOwner(), Visited);
	return Actor->HasAnyFlags(RF_WasLoaded);
}

bool ResolveAuthoredId(const AActor* Actor, TSet<const AActor*>& Visited, FGuid& OutId)
{
	if (!Actor || Visited.Contains(Actor)) return false;
	Visited.Add(Actor);
	if (const UChildActorComponent* ParentComponent = FindAuthoredParentComponent(Actor))
	{
		FGuid ParentId;
		if (!ResolveAuthoredId(ParentComponent->GetOwner(), Visited, ParentId)) return false;
		const FString Key = ParentId.ToString(EGuidFormats::Digits) + TEXT("|") + ParentComponent->GetName();
		const FTCHARToUTF8 Bytes(*Key);
		FMD5 Md5;
		Md5.Update(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
		uint8 Digest[16];
		Md5.Final(Digest);
		uint32 Words[4];
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const uint8* Word = Digest + Index * 4;
			Words[Index] = (uint32(Word[0]) << 24) | (uint32(Word[1]) << 16) | (uint32(Word[2]) << 8) | uint32(Word[3]);
		}
		OutId = FGuid(Words[0], Words[1], Words[2], Words[3]);
		return OutId.IsValid() && OutId != ParentId;
	}
	int32 Count = 0;
	for (FName Tag : Actor->Tags)
	{
		const FString Value = Tag.ToString();
		if (!Value.StartsWith(IdTagPrefix)) continue;
		++Count;
		if (!FGuid::Parse(Value.RightChop(IdTagPrefix.Len()), OutId)) return false;
	}
	return Count == 1 && OutId.IsValid();
}
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
	TSet<const AActor*> Visited;
	if (!ResolveAuthoredId(Owner, Visited, FoundId)) return false;
	StableId = FoundId;
	if (FindAuthoredParentComponent(Owner)) SetLevelInstanceId(FoundId);
	return true;
}

bool USWRoomSnapshotComponent::IsAuthoredRoomActor(const AActor* Actor)
{
	TSet<const AActor*> Visited;
	return IsAuthored(Actor, Visited);
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
	ReleaseAuthoredChildBinding();
	if (IsAuthoredRoomActor(GetOwner())) RefreshLevelInstanceId();
	// The parent component's ChildActor pointer is assigned after SpawnActor
	// returns. Its override parent is already available during registration.
	if (UChildActorComponent* Parent = GetOwner() ? GetOwner()->GetParentComponent() : nullptr)
	{
		AuthoredParentComponent = Parent;
		ChildActorCreatedHandle = Parent->OnChildActorCreated().AddUObject(this, &USWRoomSnapshotComponent::HandleAuthoredChildCreated);
	}
}

void USWRoomSnapshotComponent::OnUnregister()
{
	ReleaseAuthoredChildBinding();
	Super::OnUnregister();
}

void USWRoomSnapshotComponent::ReleaseAuthoredChildBinding()
{
	if (UChildActorComponent* Parent = AuthoredParentComponent.Get())
		Parent->OnChildActorCreated().Remove(ChildActorCreatedHandle);
	ChildActorCreatedHandle.Reset();
	AuthoredParentComponent.Reset();
}

void USWRoomSnapshotComponent::HandleAuthoredChildCreated(AActor* ChildActor)
{
	// FinishSpawning applies cached component instance data after OnRegister.
	// Refresh once construction is complete so that cached template IDs cannot
	// replace the existing authored parent/component identity contract.
	if (ChildActor == GetOwner() && IsAuthoredRoomActor(ChildActor)) RefreshLevelInstanceId();
}

void USWRoomSnapshotComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseAuthoredChildBinding();
#if WITH_EDITOR
	FTSTicker::GetCoreTicker().RemoveTicker(EditorDuplicateIdTicker);
	EditorDuplicateIdTicker.Reset(); bPendingEditorDuplicateId = false;
#endif
	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void USWRoomSnapshotComponent::BeginEditorVoyageMigration()
{
	check(IsInGameThread());
	++EditorVoyageMigrationDepth;
	for (TObjectIterator<USWRoomSnapshotComponent> It; It; ++It)
	{
		It->bPendingEditorDuplicateId = false;
		FTSTicker::GetCoreTicker().RemoveTicker(It->EditorDuplicateIdTicker);
		It->EditorDuplicateIdTicker.Reset();
	}
}

void USWRoomSnapshotComponent::EndEditorVoyageMigration()
{
	check(IsInGameThread() && EditorVoyageMigrationDepth > 0);
	--EditorVoyageMigrationDepth;
}

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
	if (EditorVoyageMigrationDepth > 0 || IsTemplate() || bPendingEditorDuplicateId) return;
	bPendingEditorDuplicateId = true;
	EditorDuplicateIdAttempts = 0;
	const TWeakObjectPtr<USWRoomSnapshotComponent> WeakThis(this);
	EditorDuplicateIdTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float DeltaTime)
	{
		return WeakThis.IsValid() ? WeakThis->ApplyPendingEditorDuplicateId(DeltaTime) : false;
	}));
}

bool USWRoomSnapshotComponent::ApplyPendingEditorDuplicateId(float DeltaTime)
{
	(void)DeltaTime;
	if (EditorVoyageMigrationDepth > 0 || !bPendingEditorDuplicateId) return false;
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
