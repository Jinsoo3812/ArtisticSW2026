#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SWRoomProgressSubsystem.generated.h"

class USWRoomSaveGame;

UCLASS()
class ARTISTICSWCORE_API USWRoomProgressSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	bool IsHostedRoom() const { return bHostedRoom; }
	const USWRoomSaveGame* GetActiveRoom() const { return ActiveRoom; }
	USWRoomSaveGame* GetMutableActiveRoom() { return ActiveRoom; }
	bool WriteCheckpoint();
	bool IsNewRoomPending() const { return bNewRoomPending; }
	bool IsNewRoomCommitted() const { return bNewRoomCommitted; }
	bool HasStartupError() const { return bStartupError; }
	FGuid GetHostKey() const { return HostKey; }
	int32 AdvanceRestoreGeneration() { return ++RestoreGeneration; }
	void MarkReturnTravelPending() { bReturnTravelPending = true; }
	void ClearReturnTravelPending() { bReturnTravelPending = false; }
	bool IsReturnTravelPending() const { return bReturnTravelPending; }
	void MarkGameOverTravelPending() { bGameOverTravelPending = true; }
	void ClearGameOverTravelPending() { bGameOverTravelPending = false; }
	bool IsGameOverTravelPending() const { return bGameOverTravelPending; }
private:
	UPROPERTY(Transient) TObjectPtr<USWRoomSaveGame> ActiveRoom;
	FGuid HostKey;
	bool bHostedRoom = false;
	bool bStartupError = false;
	bool bReturnTravelPending = false;
	bool bGameOverTravelPending = false;
	bool bNewRoomPending = false;
	bool bNewRoomCommitted = false;
	bool bSaving = false;
	int32 RestoreGeneration = 0;
};
