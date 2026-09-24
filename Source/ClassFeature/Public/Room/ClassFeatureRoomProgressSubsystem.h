#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "ClassFeatureRoomProgressSubsystem.generated.h"

class ABasePlayer;

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
	bool TryReturn(UWorld* World, ABasePlayer* Requester);
private:
	UFUNCTION() void HandleGameOverRestart();
	void HandlePostLoadMap(UWorld* World);
	bool TickRestore(float DeltaTime);
	FDelegateHandle PostLoadHandle;
	FTSTicker::FDelegateHandle RestoreTickerHandle;
	TWeakObjectPtr<UWorld> PendingWorld;
	double RestoreDeadline = 0.0;
	bool bReturning = false;
};
