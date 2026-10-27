#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Audio/SWFoleyTypes.h"
#include "SWAnimNotify_EnemyFootContact.generated.h"

/** Contact marker on enemy locomotion clips; player notify timing is independent. */
UCLASS(meta = (DisplayName = "Artistic SW Enemy Foot Contact"))
class ENEMY_API USWAnimNotify_EnemyFootContact : public UAnimNotify
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley")
	ESWFoleySide Side = ESWFoleySide::Left;
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
