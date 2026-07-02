#include "SWCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "SwimmingComponent.h"

void USWCharacterMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
	Super::PhysCustom(DeltaTime, Iterations);

	if (CustomMovementMode == static_cast<uint8>(ECustomMovementMode::CMOVE_Swimming))
	{
		if (ACharacter* CharOwner = CharacterOwner)
		{
			if (USwimmingComponent* SwimComp = CharOwner->FindComponentByClass<USwimmingComponent>())
			{
				SwimComp->UpdateSwimmingMovement(DeltaTime);
			}
		}
	}
}

void USWCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);

	if (ACharacter* CharOwner = CharacterOwner)
	{
		if (USwimmingComponent* SwimComp = CharOwner->FindComponentByClass<USwimmingComponent>())
		{
			SwimComp->CheckWaterTransitions();
		}
	}
}
