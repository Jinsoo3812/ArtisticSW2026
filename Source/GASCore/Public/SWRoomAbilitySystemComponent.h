#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "Room/SWRoomSnapshotTypes.h"
#include "SWRoomAbilitySystemComponent.generated.h"

/** Server-side value capture for duration and infinite effects. */
UCLASS()
class GASCORE_API USWRoomAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()
public:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	void CaptureRoomEffects(TArray<FSWRoomGameplayEffectState>& OutEffects, TArray<FSWRoomCaptureIssue>& OutIssues) const;
	bool RestoreRoomEffects(const TArray<FSWRoomGameplayEffectState>& Effects, FString& OutError);
private:
	void HandleRoomEffectRemoved(const FActiveGameplayEffect& Effect);
	TMap<FActiveGameplayEffectHandle, FSWRoomGameplayEffectState> RestoredSourceMetadata;
	FDelegateHandle RemovedEffectDelegateHandle;
	bool bRestoringRoomEffects = false;
};
