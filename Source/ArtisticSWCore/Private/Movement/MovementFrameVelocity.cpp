#include "Movement/MovementFrameVelocity.h"

#include "Movement/MovementFrameVelocityProvider.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

USceneComponent* MovementFrameVelocity::GetCarrier(const AActor* Actor, FName& OutBoneName)
{
	OutBoneName = NAME_None;
	if (!IsValid(Actor)) return nullptr;
	if (const ACharacter* Character = Cast<ACharacter>(Actor))
	{
		const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		if (Movement && Movement->IsMovingOnGround() && Character->GetMovementBase())
		{
			OutBoneName = Character->GetBasedMovement().BoneName;
			return Character->GetMovementBase();
		}
	}
	const USceneComponent* Root = Actor->GetRootComponent();
	if (Root && Root->GetAttachParent())
	{
		OutBoneName = Root->GetAttachSocketName();
		return Root->GetAttachParent();
	}
	return nullptr;
}

bool MovementFrameVelocity::TryGetPointVelocity(USceneComponent* Carrier, FName BoneName,
	const FVector& WorldPoint, FVector& OutVelocity)
{
	OutVelocity = FVector::ZeroVector;
	if (!IsValid(Carrier) || WorldPoint.ContainsNaN()) return false;

	for (USceneComponent* Component = Carrier; Component; Component = Component->GetAttachParent())
	{
		if (const auto* Provider = Cast<IMovementFrameVelocityProvider>(Component->GetOwner()))
		{
			return Provider->TryGetMovementFrameVelocityAtPoint(WorldPoint, OutVelocity)
				&& !OutVelocity.ContainsNaN();
		}
		if (UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Component))
		{
			const FName BodyBone = Component == Carrier ? BoneName : NAME_None;
			if (Body->IsSimulatingPhysics(BodyBone))
			{
				OutVelocity = Body->GetPhysicsLinearVelocityAtPoint(WorldPoint, BodyBone);
				return !OutVelocity.ContainsNaN();
			}
		}
	}

	// ComponentVelocity alone cannot prove a kinematic mover has no angular motion.
	// Such movers must provide point velocity explicitly; stationary ground is known zero.
	return Carrier->Mobility != EComponentMobility::Movable;
}

bool MovementFrameVelocity::TryGetActorPointVelocity(const AActor* Actor,
	const FVector& WorldPoint, FVector& OutVelocity)
{
	OutVelocity = FVector::ZeroVector;
	if (!IsValid(Actor) || WorldPoint.ContainsNaN()) return false;

	if (const ACharacter* Character = Cast<ACharacter>(Actor))
	{
		const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		if (!Movement) return false;
		FName BoneName;
		if (USceneComponent* Carrier = GetCarrier(Character, BoneName))
		{
			if (!TryGetPointVelocity(Carrier, BoneName, WorldPoint, OutVelocity)) return false;
			// Disabled movement is used by attached passengers and can retain stale velocity.
			if (Movement->MovementMode != MOVE_None) OutVelocity += Movement->Velocity;
		}
		else
		{
			OutVelocity = Movement->MovementMode == MOVE_None ? FVector::ZeroVector : Movement->Velocity;
		}
		return !OutVelocity.ContainsNaN();
	}

	FName BoneName;
	if (USceneComponent* Carrier = GetCarrier(Actor, BoneName))
	{
		// Attached actors have no generic relative-velocity contract. Their carrier owns motion.
		return TryGetPointVelocity(Carrier, BoneName, WorldPoint, OutVelocity);
	}
	if (UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
	{
		if (Body->IsSimulatingPhysics())
		{
			OutVelocity = Body->GetPhysicsLinearVelocityAtPoint(WorldPoint);
			return !OutVelocity.ContainsNaN();
		}
	}
	OutVelocity = Actor->GetVelocity();
	return !OutVelocity.ContainsNaN();
}
