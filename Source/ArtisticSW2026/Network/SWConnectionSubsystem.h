#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "SWConnectionTypes.h"
#include "SWConnectionSubsystem.generated.h"

class STextBlock;
class SWidget;
class UNetDriver;
class UGameViewportClient;
class UWorld;

UCLASS()
class ARTISTICSW2026_API USWConnectionSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	bool IsLoadingPresentationVisible() const { return bLoadingPresentationVisible; }
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;

	UFUNCTION(BlueprintCallable)
	bool ConnectDirect(const FString& Address);
	bool ConnectDirectWithName(const FString& Address, const FString& DisplayName, const FGuid& HostKey = FGuid());
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
	UFUNCTION(BlueprintPure)
	FString GetReadinessDebugStatus() const;
	bool BeginRoomReturnPresentation();
	bool BeginRoomFinalDeparturePresentation(int32 AttemptId);
	void CancelRoomReturnPresentation();

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
	uint8 BuildReadinessMask() const;
	void StartReadinessCheck(UWorld* LoadedWorld);
	void StopReadinessCheck();
	void CompleteReadiness();
	bool IsFailureRelevantToThisInstance(const UWorld* FailureWorld) const;
	void ShowLoadingPresentation();
	void HideLoadingPresentation();
	void UpdateLoadingPresentationText();

	ESWConnectionState ConnectionState = ESWConnectionState::Idle;
	FSWConnectionFailure LastFailure;
	int32 AttemptSerial = 0;
	int32 ActiveAttemptId = 0;
	double DiagnosticConnectStartedAt = 0.0;
	bool bConnectionAttemptActive = false;
	bool bIntentionalDisconnect = false;
	TWeakObjectPtr<UWorld> ReadinessWorld;
	bool bReadinessCheckActive = false;
	float ReadinessElapsedSeconds = 0.0f;
	int32 ConsecutiveReadyTicks = 0;
	uint8 LastLoggedReadinessMask = 0;
	static constexpr float ReadinessTimeoutSeconds = 30.0f;
	static constexpr int32 RequiredConsecutiveReadyTicks = 3;
	TSharedPtr<SWidget> LoadingOverlayWidget;
	TSharedPtr<STextBlock> LoadingStatusText;
	TWeakObjectPtr<UGameViewportClient> LoadingViewport;
	bool bLoadingPresentationVisible = false;
	bool bViewportIgnoredInputBeforeLoading = false;
	enum class ERoomLoadingReason : uint8 { None, Return, FinalDeparture };
	ERoomLoadingReason RoomLoadingReason = ERoomLoadingReason::None;
	int32 FinalDepartureAttemptId = 0;
	FString PendingDisplayName;
	FGuid PendingHostKey;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
	FDelegateHandle PreLoadMapHandle;
	FDelegateHandle PostLoadMapHandle;
};
