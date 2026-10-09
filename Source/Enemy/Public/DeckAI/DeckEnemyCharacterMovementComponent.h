#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DeckEnemyCharacterMovementComponent.generated.h"

/** Observe deck walking without changing attack, hit-reaction or client movement. */
UCLASS()
class ENEMY_API UDeckEnemyCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:
	virtual void HandleImpact(const FHitResult& Hit, float TimeSlice = 0.f,
		const FVector& MoveDelta = FVector::ZeroVector) override;
	virtual FVector GetPenetrationAdjustment(const FHitResult& Hit) const override;
};
