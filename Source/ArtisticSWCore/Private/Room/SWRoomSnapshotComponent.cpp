#include "Room/SWRoomSnapshotComponent.h"
#include "GameFramework/Actor.h"

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
	if (GetOwner() && !IsTemplate()) SetLevelInstanceId(FGuid::NewGuid());
}

void USWRoomSnapshotComponent::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	if (DuplicateMode != EDuplicateMode::PIE && GetOwner() && !GetOwner()->HasAnyFlags(RF_WasLoaded)
		&& !IsTemplate() && GetWorld() && GetWorld()->WorldType == EWorldType::Editor)
		SetLevelInstanceId(FGuid::NewGuid());
}
#endif
