#include "Animation/SWAnimNotify_FoleyEvent.h"
#include "Animation/AnimNotifyLibrary.h"
#include "Components/SWFoleyComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

USWAnimNotify_FoleyEvent::USWAnimNotify_FoleyEvent()
{
	FoleyEvent.Event = SW::Foley::Walk;
}

void USWAnimNotify_FoleyEvent::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	if (!MeshComp || UAnimNotifyLibrary::IsBlendingOut(EventReference)) { return; }
	if (AActor* Owner = MeshComp->GetOwner())
	{
		if (auto* Foley = Owner->FindComponentByClass<USWFoleyComponent>())
		{
			Foley->HandleAnimNotify(MeshComp, FoleyEvent);
		}
	}
}

FString USWAnimNotify_FoleyEvent::GetNotifyName_Implementation() const
{
	return FString::Printf(TEXT("Foley: %s (%s)"), *FoleyEvent.Event.ToString(),
		*UEnum::GetValueAsString(FoleyEvent.Side));
}
