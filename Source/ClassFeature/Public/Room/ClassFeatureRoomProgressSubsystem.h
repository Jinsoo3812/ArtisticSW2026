#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "TimerManager.h"
#include "Room/SWRoomSnapshotTypes.h"
#include "ClassFeatureRoomProgressSubsystem.generated.h"

class ABasePlayer;
class ABasePlayerController;

UCLASS()
class CLASSFEATURE_API UClassFeatureRoomProgressSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	bool RestoreSharedWorld(UWorld* World);
	bool CaptureSharedWorld(UWorld* World);
	void RestorePlayer(ABasePlayer* Player);
	void CapturePlayer(ABasePlayer* Player);
	bool TrySave(UWorld* World, ESWRoomSaveKind Kind, FString& OutError);
	int32 GetLastCaptureIssueCount() const { return LastCaptureIssueCount; }
	bool TryReturn(UWorld* World, ABasePlayer* Requester);
	void ConfirmReturnPresentation(ABasePlayerController* Controller);
private:
	void BeginReturnTravel();
	void HandleReturnPresentationTimeout();
	void CancelReturnPresentation();
	UFUNCTION() void HandleGameOverRestart();
	void HandlePostLoadMap(UWorld* World);
	bool TickRestore(float DeltaTime);
	FDelegateHandle PostLoadHandle;
	FTSTicker::FDelegateHandle RestoreTickerHandle;
	TWeakObjectPtr<UWorld> PendingWorld;
	double RestoreDeadline = 0.0;
	bool bReturning = false;
	TArray<TWeakObjectPtr<ABasePlayerController>> ReturnControllers;
	TArray<TWeakObjectPtr<ABasePlayerController>> PendingReturnControllers;
	FTimerHandle ReturnPresentationTimeoutHandle;
	bool bWorldSnapshotRestored = false;
	bool bReturnShipPlaced = false;
	bool bShipSafetyFallbackUsed = false;
	double ShipSafetyCheckAt = 0.0;
	bool bSaving = false;
	int32 LastCaptureIssueCount = 0;
};
