#include "AN_SendGameplayEvent.h"
#include "AbilitySystemBlueprintLibrary.h"

UAN_SendGameplayEvent::UAN_SendGameplayEvent()
{
#if WITH_EDITORONLY_DATA
	// 에디터 타임라인에서 눈에 띄게 주황색으로 표시
	NotifyColor = FColor(255, 128, 0, 255);
#endif
}

void UAN_SendGameplayEvent::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	// 메시와 오너 액터가 유효한지 확인
	if (MeshComp && MeshComp->GetOwner())
	{
		AActor* OwnerActor = MeshComp->GetOwner();

		// 태그가 세팅되어 있을 때만 실행
		if (EventTag.IsValid())
		{
			// 빈 페이로드(Payload) 생성
			FGameplayEventData Payload;
			Payload.Instigator = OwnerActor;
			Payload.Target = OwnerActor;
			Payload.EventTag = EventTag;

			// 액터에게 이벤트를 전송 (해당 액터의 ASC가 받아서 처리함)
			UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(OwnerActor, EventTag, Payload);
		}
	}
}

FString UAN_SendGameplayEvent::GetNotifyName_Implementation() const
{
	// 에디터의 노티파이 트랙에 "AN_SendGameplayEvent" 대신 태그 이름이 출력되게 만듦
	if (EventTag.IsValid())
	{
		return EventTag.ToString();
	}
	return Super::GetNotifyName_Implementation();
}