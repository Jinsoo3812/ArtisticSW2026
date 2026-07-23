#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ANS_HitScanWindow.generated.h"

/**
 * Animation-synchronized melee hit-scan window.
 *
 * Begin, Tick, and End are forwarded as gameplay events. The active gameplay
 * ability owns the damage spec, while the weapon owns the trace state.
 */
UCLASS(meta = (DisplayName = "Hit Scan Window"))
class ARTISTICSWCORE_API UANS_HitScanWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UANS_HitScanWindow();

	virtual void NotifyBegin(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyTick(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		float FrameDeltaTime,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;
};
