#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Audio/SWFoleyTypes.h"
#include "SWAnimNotify_SwimStroke.generated.h"

/** Stateless surface hand-catch timing. Each listener hears its own replicated animation pose. */
UCLASS(meta = (DisplayName = "Artistic SW Surface Swim Stroke"))
class CLASSFEATURE_API USWAnimNotify_SwimStroke : public UAnimNotify
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swimming")
	ESWFoleySide Side = ESWFoleySide::Left;
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
