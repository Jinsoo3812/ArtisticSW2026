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
	bool HasStartupError() const { return bStartupError; }
	FGuid GetHostKey() const { return HostKey; }
	void MarkReturnTravelPending() { bReturnTravelPending = true; }
	void ClearReturnTravelPending() { bReturnTravelPending = false; }
	bool IsReturnTravelPending() const { return bReturnTravelPending; }
private:
	UPROPERTY(Transient) TObjectPtr<USWRoomSaveGame> ActiveRoom;
	FGuid HostKey;
	bool bHostedRoom = false;
	bool bStartupError = false;
	bool bReturnTravelPending = false;
};
