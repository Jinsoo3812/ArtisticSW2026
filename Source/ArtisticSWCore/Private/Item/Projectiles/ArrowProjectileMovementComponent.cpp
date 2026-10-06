#include "Item/Projectiles/ArrowProjectileMovementComponent.h"

#include "Item/Projectiles/ArrowCollisionQuery.h"
#include "Item/Projectiles/ArrowProjectile.h"

void UArrowProjectileMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* TickFunction)
{
	// A late-frame spawn must not integrate a whole frame that occurred before release.
	if (LaunchFrame == GFrameCounter) return;
	Super::TickComponent(DeltaTime, TickType, TickFunction);
}

bool UArrowProjectileMovementComponent::MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& NewRotation,
	bool bSweep, FHitResult* OutHit, ETeleportType Teleport)
{
	AArrowProjectile* Arrow = Cast<AArrowProjectile>(GetOwner());
	if (!Arrow || !UpdatedComponent)
		return Super::MoveUpdatedComponentImpl(Delta, NewRotation, bSweep, OutHit, Teleport);

	FHitResult Hit(1.0f);
	bool bBlocked = false;
	if (bSweep && Teleport == ETeleportType::None && Arrow->HasAuthority())
	{
		const FVector Start = UpdatedComponent->GetComponentLocation();
		bBlocked = ArrowCollisionQuery::SweepFlight(*Arrow, Start, Start + Delta, NewRotation, Hit);
	}
	// Do not run a second engine sweep with the large legacy BoxComp. Collision was
	// already resolved above, before either damage or StopSimulating can occur.
	const bool bMoved = Super::MoveUpdatedComponentImpl(Delta * (bBlocked ? Hit.Time : 1.0f),
		NewRotation, false, nullptr, Teleport);
	if (OutHit) *OutHit = Hit;
	return bMoved;
}

UProjectileMovementComponent::EHandleBlockingHitResult UArrowProjectileMovementComponent::HandleBlockingHit(
	const FHitResult& Hit, float TimeTick, const FVector& MoveDelta, float& SubTickTimeRemaining)
{
	SubTickTimeRemaining = 0.0f;
	// The engine has now advanced velocity to impact time. Preserve it for damage/FX
	// before StopSimulating zeroes the velocity. Initial penetrations take this path too.
	UpdateComponentVelocity();
	if (AArrowProjectile* Arrow = Cast<AArrowProjectile>(GetOwner())) Arrow->HandleFlightImpact(Hit);
	if (UpdatedComponent) StopSimulating(Hit);
	return EHandleBlockingHitResult::Abort;
}
