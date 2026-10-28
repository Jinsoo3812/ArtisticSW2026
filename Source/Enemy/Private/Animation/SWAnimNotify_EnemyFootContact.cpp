#include "Animation/SWAnimNotify_EnemyFootContact.h"

#include "Animation/AnimNotifyLibrary.h"
#include "BaseEnemy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SWFoleyComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

void USWAnimNotify_EnemyFootContact::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	if (!MeshComp || UAnimNotifyLibrary::IsBlendingOut(EventReference)) { return; }
	auto* Enemy = Cast<ABaseEnemy>(MeshComp->GetOwner());
	if (!Enemy || MeshComp != Enemy->GetMesh()) { return; }
	const auto* Movement = Enemy->GetCharacterMovement();
	// CharacterMovement velocity excludes transport by the movement base. A ship
	// carrying a stationary passenger must not turn a blended jog into footsteps.
	if (!Movement || !Movement->IsMovingOnGround() || Movement->Velocity.SizeSquared2D() < FMath::Square(5.f)) { return; }
	if (auto* Foley = Enemy->FindComponentByClass<USWFoleyComponent>())
	{
		FSWFoleyEvent Event;
		Event.Event = Movement->Velocity.SizeSquared2D() > FMath::Square(280.f) ? SW::Foley::Run : SW::Foley::Walk;
		Event.Side = Side;
		Foley->HandleAnimNotify(MeshComp, Event);
	}
}

FString USWAnimNotify_EnemyFootContact::GetNotifyName_Implementation() const
{
	return Side == ESWFoleySide::Left ? TEXT("Enemy foot: L") : TEXT("Enemy foot: R");
}
