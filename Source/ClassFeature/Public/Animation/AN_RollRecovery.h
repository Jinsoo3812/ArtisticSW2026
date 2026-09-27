#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AN_RollRecovery.generated.h"

/**
 * Marks the point where roll gameplay control may return to locomotion.
 * The notify emits an event only; UGA_PlayerRoll owns ability completion and
 * montage blend-out policy.
 */
UCLASS(meta = (DisplayName = "Roll Recovery"))
class CLASSFEATURE_API UAN_RollRecovery : public UAnimNotify
{
	GENERATED_BODY()

public:
	UAN_RollRecovery();

	virtual void Notify(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;
};
