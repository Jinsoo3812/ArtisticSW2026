#include "Animation/SWAnimNotify_SwimStroke.h"
#include "Animation/AnimNotifyLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SWFoleyComponent.h"
#include "GameFramework/Actor.h"

void USWAnimNotify_SwimStroke::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	if (!MeshComp || UAnimNotifyLibrary::IsBlendingOut(EventReference)) { return; }
	if (auto* Owner = MeshComp->GetOwner())
	{
		if (auto* Foley = Owner->FindComponentByClass<USWFoleyComponent>())
		{
			Foley->HandleSurfaceSwimStroke(MeshComp, Side, Animation);
		}
	}
}

FString USWAnimNotify_SwimStroke::GetNotifyName_Implementation() const
{
	return Side == ESWFoleySide::Left ? TEXT("Swim catch: L") : TEXT("Swim catch: R");
}
