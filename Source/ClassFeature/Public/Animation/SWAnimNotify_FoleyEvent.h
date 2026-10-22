#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Audio/SWFoleyTypes.h"
#include "SWAnimNotify_FoleyEvent.generated.h"

/** Stateless timing bridge. Does not send GAS events or network RPCs. */
UCLASS(meta = (DisplayName = "Artistic SW Foley Event"))
class CLASSFEATURE_API USWAnimNotify_FoleyEvent : public UAnimNotify
{
	GENERATED_BODY()
public:
	USWAnimNotify_FoleyEvent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley")
	FSWFoleyEvent FoleyEvent;
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
