#include "DeckAI/DeckEnemyCharacterMovementComponent.h"
#include "DeckAI/DeckWalkRouteComponent.h"
#include "GameFramework/Character.h"

void UDeckEnemyCharacterMovementComponent::HandleImpact(const FHitResult& Hit, float TimeSlice, const FVector& MoveDelta)
{
	Super::HandleImpact(Hit, TimeSlice, MoveDelta);
	if (UDeckWalkRouteComponent* Route = CharacterOwner ? CharacterOwner->FindComponentByClass<UDeckWalkRouteComponent>() : nullptr)
		Route->ObserveMovementImpact(Hit, MoveDelta);
}

FVector UDeckEnemyCharacterMovementComponent::GetPenetrationAdjustment(const FHitResult& Hit) const
{
	if (UDeckWalkRouteComponent* Route = CharacterOwner ? CharacterOwner->FindComponentByClass<UDeckWalkRouteComponent>() : nullptr)
	{
		Route->ObserveMovementImpact(Hit, FVector::ZeroVector);
		FVector SafeAdjustment;
		if (Route->TryGetSafePenetrationAdjustment(Hit, SafeAdjustment)) return SafeAdjustment;
	}
	return Super::GetPenetrationAdjustment(Hit);
}
