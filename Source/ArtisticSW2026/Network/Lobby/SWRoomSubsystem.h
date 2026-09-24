#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Network/SWConnectionTypes.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IHttpRequest.h"
#include "SWRoomSubsystem.generated.h"

UENUM(BlueprintType)
enum class ESWRoomState : uint8
{
	Idle, ResolvingPublicIP, StartingServer, Connecting, Playing, Failed, Stopping
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSWRoomChanged, ESWRoomState, State, FText, Message);

UCLASS()
class ARTISTICSW2026_API USWRoomSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return !IsTemplate(); }
	virtual TStatId GetStatId() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;
	UFUNCTION(BlueprintCallable) bool CreateRoom(const FString& DisplayName, const FString& OptionalPublicIPv4);
	UFUNCTION(BlueprintCallable) bool ContinueRoom(const FString& DisplayName, const FString& OptionalPublicIPv4);
	UFUNCTION(BlueprintCallable) bool JoinRoom(const FString& DisplayName, const FString& RoomCode);
	UFUNCTION(BlueprintCallable) bool ConnectHostedRoom();
	UFUNCTION(BlueprintCallable) void CancelPendingOperation();
	UFUNCTION(BlueprintCallable) void LeaveRoom();
	UFUNCTION(BlueprintPure) ESWRoomState GetRoomState() const { return State; }
	UFUNCTION(BlueprintPure) FString GetRoomCode() const { return DisplayCode; }
	UFUNCTION(BlueprintPure) FText GetRoomMessage() const { return Message; }
	UFUNCTION(BlueprintPure) bool NeedsManualPublicIP() const { return bNeedsManualPublicIP; }
	UFUNCTION(BlueprintPure) bool CanHost() const;
	UFUNCTION(BlueprintPure) bool HasSavedRoom() const;
	UPROPERTY(BlueprintAssignable) FOnSWRoomChanged OnRoomChanged;
private:
	UFUNCTION() void HandleConnectionChanged(ESWConnectionState Previous, ESWConnectionState Current, int32 AttemptId);
	UFUNCTION() void HandleConnectionFailed(FSWConnectionFailure Failure);
	void SetState(ESWRoomState NewState, const FText& NewMessage);
	void Fail(const FText& FailureMessage);
	void StartServer(const FString& PublicAddress);
	bool BeginHosting(const FString& Name, const FString& OptionalPublicIPv4, bool bContinue);
	void StopServer();
	void ReturnToLobby();
	FString MarkerPath() const;
	FString DisplayName;
	FString PublicAddress;
	FString DisplayCode;
	FText Message;
	ESWRoomState State = ESWRoomState::Idle;
	uint64 OperationId = 0;
	FGuid RoomRunId;
	FGuid SavedRoomId;
	FGuid HostKey;
	bool bContinuingRoom = false;
	FProcHandle ServerHandle;
	uint32 ServerPid = 0;
	bool bOwnsServer = false;
	bool bAwaitingHostJoin = false;
	bool bNeedsManualPublicIP = false;
	bool bReturningToLobby = false;
	float ServerStartElapsed = 0;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> PendingRequest;
};
