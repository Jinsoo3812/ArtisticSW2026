#pragma once

#include "CoreMinimal.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "ArrowProjectileMovementComponent.generated.h"

/** Keeps engine gravity/substeps; delegates each segment's collision to the shared arrow query. */
UCLASS()
class ARTISTICSWCORE_API UArrowProjectileMovementComponent : public UProjectileMovementComponent
{
	GENERATED_BODY()

public:
	void MarkLaunchFrame() { LaunchFrame = GFrameCounter; }
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;

protected:
	virtual bool MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& NewRotation,
		bool bSweep, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) override;
	virtual EHandleBlockingHitResult HandleBlockingHit(const FHitResult& Hit, float TimeTick,
		const FVector& MoveDelta, float& SubTickTimeRemaining) override;

private:
	uint64 LaunchFrame = MAX_uint64;
};
