#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SWConnectionTypes.h"
#include "SWConnectionSubsystem.generated.h"

class UNetDriver;
class UWorld;

UCLASS()
class ARTISTICSW2026_API USWConnectionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable)
	bool ConnectDirect(const FString& Address);
	UFUNCTION(BlueprintCallable)
	void DisconnectToDefaultMap();
	UFUNCTION(BlueprintCallable)
	void ResetFailure();
	UFUNCTION(BlueprintPure)
	ESWConnectionState GetConnectionState() const { return ConnectionState; }
	UFUNCTION(BlueprintPure)
	FSWConnectionFailure GetLastFailure() const { return LastFailure; }
	UFUNCTION(BlueprintPure)
	int32 GetActiveAttemptId() const { return ActiveAttemptId; }

	UPROPERTY(BlueprintAssignable)
	FOnSWConnectionStateChanged OnConnectionStateChanged;
	UPROPERTY(BlueprintAssignable)
	FOnSWConnectionFailed OnConnectionFailed;

private:
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);
	void HandlePreLoadMap(const FString& MapName);
	void HandlePostLoadMap(UWorld* LoadedWorld);
	void TransitionTo(ESWConnectionState NewState);
	void RecordFailure(ESWConnectionFailureReason Reason, const FString& EngineFailureType, const FString& EngineMessage);
	ESWConnectionFailureReason ClassifyNetworkFailure(ENetworkFailure::Type FailureType, const FString& ErrorString) const;

	ESWConnectionState ConnectionState = ESWConnectionState::Idle;
	FSWConnectionFailure LastFailure;
	int32 AttemptSerial = 0;
	int32 ActiveAttemptId = 0;
	bool bConnectionAttemptActive = false;
	bool bIntentionalDisconnect = false;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
	FDelegateHandle PreLoadMapHandle;
	FDelegateHandle PostLoadMapHandle;
};
