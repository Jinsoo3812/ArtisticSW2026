#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_DeathRagdoll.generated.h"

/**
 * Marks the authored transition point from a death montage into ragdoll.
 * The server finishes the replicated death state; every machine then applies
 * ragdoll through its owner's OnDeathFinished handler.
 */
UCLASS(meta = (DisplayName = "Death Ragdoll Point"))
class GASCORE_API UAnimNotify_DeathRagdoll : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;
};
