#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Room/SWRoomSnapshotTypes.h"
#include "SWRoomSnapshotComponent.generated.h"

/** Declares an actor's persistent room identity and the values this actor owns. */
UCLASS(ClassGroup=(Room), meta=(BlueprintSpawnableComponent))
class ARTISTICSWCORE_API USWRoomSnapshotComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USWRoomSnapshotComponent();

	UPROPERTY(EditInstanceOnly, Category="Room Snapshot") FGuid StableId;
	UPROPERTY(EditDefaultsOnly, Category="Room Snapshot", meta=(ClampMin="1")) int32 ContractVersion = 1;
	UPROPERTY(EditDefaultsOnly, Category="Room Snapshot") ESWRoomPersistenceClass PersistenceClass = ESWRoomPersistenceClass::ManualOnly;
	UPROPERTY(EditDefaultsOnly, Category="Room Snapshot") bool bRequired = false;

	void SetRuntimeId(const FGuid& Id) { StableId = Id; }
	void SetRuntimeOrigin(const FGuid& InCreatorId, uint64 InSequence)
	{ CreatorId = InCreatorId; CreatorSequence = InSequence; }
	FGuid GetCreatorId() const { return CreatorId; }
	uint64 GetCreatorSequence() const { return CreatorSequence; }
	bool RefreshLevelInstanceId();
	void SetLevelInstanceId(const FGuid& Id);
	virtual void OnRegister() override;
#if WITH_EDITOR
	virtual void OnComponentCreated() override;
	virtual void PostEditImport() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
#endif
private:
	FGuid CreatorId;
	uint64 CreatorSequence = 0;
#if WITH_EDITOR
	void QueueEditorDuplicateId();
	bool ApplyPendingEditorDuplicateId(float DeltaTime);
	bool bPendingEditorDuplicateId = false;
	int32 EditorDuplicateIdAttempts = 0;
#endif
};
