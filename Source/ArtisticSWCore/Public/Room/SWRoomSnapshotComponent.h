#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Components/ActorComponent.h"
#include "Room/SWRoomSnapshotTypes.h"
#include "SWRoomSnapshotComponent.generated.h"

class UChildActorComponent;

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
	static bool IsAuthoredRoomActor(const AActor* Actor);
	void SetLevelInstanceId(const FGuid& Id);
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
	static void BeginEditorVoyageMigration();
	static void EndEditorVoyageMigration();
	virtual void OnComponentCreated() override;
	virtual void PostEditImport() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
#endif
private:
	void HandleAuthoredChildCreated(AActor* ChildActor);
	void ReleaseAuthoredChildBinding();
	TWeakObjectPtr<UChildActorComponent> AuthoredParentComponent;
	FDelegateHandle ChildActorCreatedHandle;
	FGuid CreatorId;
	uint64 CreatorSequence = 0;
#if WITH_EDITOR
	void QueueEditorDuplicateId();
	bool ApplyPendingEditorDuplicateId(float DeltaTime);
	bool bPendingEditorDuplicateId = false;
	int32 EditorDuplicateIdAttempts = 0;
	FTSTicker::FDelegateHandle EditorDuplicateIdTicker;
#endif
};
