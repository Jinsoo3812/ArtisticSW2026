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
	bool IsHostToken(const FGuid& Token) const { return Token.IsValid() && Token == HostReconnectToken; }
	void RememberHostToken(const FGuid& Token) { if (bHostedRoom && Token.IsValid()) HostReconnectToken = Token; }
	void MarkReturnTravelPending() { bReturnTravelPending = true; }
	void ClearReturnTravelPending() { bReturnTravelPending = false; }
	bool IsReturnTravelPending() const { return bReturnTravelPending; }
private:
	UPROPERTY(Transient) TObjectPtr<USWRoomSaveGame> ActiveRoom;
	FGuid HostKey;
	FGuid HostReconnectToken;
	bool bHostedRoom = false;
	bool bStartupError = false;
	bool bReturnTravelPending = false;
};
