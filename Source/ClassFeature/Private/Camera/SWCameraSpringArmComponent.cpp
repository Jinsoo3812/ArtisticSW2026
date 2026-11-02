#include "Camera/SWCameraSpringArmComponent.h"

#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

namespace
{
	const FName CameraBlockTag(TEXT("SW.Camera.Block"));
	const FName CameraIgnoreTag(TEXT("SW.Camera.Ignore"));
}

bool USWCameraSpringArmComponent::ShouldIgnoreCameraComponent(
	const UPrimitiveComponent* Component, bool bIgnoreCharacterObstacles)
{
	if (!Component || Component->ComponentHasTag(CameraBlockTag)) return false;
	if (Component->ComponentHasTag(CameraIgnoreTag)) return true;

	bool bExplicitIgnore = false;
	bool bCharacter = false;
	const AActor* Actor = Component->GetOwner();
	// Attachment traversal handles child-actor equipment without scanning the world.
	// Do not classify every Pawn as a character: ships are Pawns too.
	for (int32 Depth = 0; Actor && Depth < 16; ++Depth)
	{
		if (Actor->ActorHasTag(CameraBlockTag)) return false;
		bExplicitIgnore |= Actor->ActorHasTag(CameraIgnoreTag);
		bCharacter |= Actor->IsA<ACharacter>();
		const AActor* Parent = Actor->GetAttachParentActor();
		// Some equipped actors use ownership before their attachment is established.
		if (!Parent && Actor->GetOwner() && Actor->GetOwner()->IsA<ACharacter>())
		{
			Parent = Actor->GetOwner();
		}
		if (Parent == Actor) return false;
		Actor = Parent;
	}
	// An unexpectedly deep/cyclic hierarchy must fail closed.
	if (Actor) return false;
	return bExplicitIgnore || (bIgnoreCharacterObstacles && bCharacter);
}

void USWCameraSpringArmComponent::UpdateDesiredArmLocation(
	bool bDoTrace, bool bDoLocationLag, bool bDoRotationLag, float DeltaTime)
{
	LastBlockingComponent.Reset();
	LastIgnoredComponentCount = 0;
	bLastSweepBudgetExhausted = false;
	Super::UpdateDesiredArmLocation(bDoTrace, bDoLocationLag, bDoRotationLag, DeltaTime);
}

FVector USWCameraSpringArmComponent::BlendLocations(const FVector& DesiredArmLocation,
	const FVector& TraceHitLocation, bool bHitSomething, float DeltaTime)
{
	const APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (!bEnableObstaclePolicy || !bHitSomething || !GetWorld()
		|| GetWorld()->GetNetMode() == NM_DedicatedServer
		|| (PawnOwner && !PawnOwner->IsLocallyControlled()))
	{
		return Super::BlendLocations(DesiredArmLocation, TraceHitLocation, bHitSomething, DeltaTime);
	}

	// Native SpringArm already calculated rotation/location lag and set PreviousArmOrigin
	// to this frame's exact sweep origin. Keep that origin (also while swimming).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SWCameraObstaclePolicy), false, GetOwner());
	const int32 SweepLimit = FMath::Clamp(MaxPolicySweeps, 1, 32);
	for (int32 Sweep = 0; Sweep < SweepLimit; ++Sweep)
	{
		FHitResult Hit;
		if (!GetWorld()->SweepSingleByChannel(Hit, PreviousArmOrigin, DesiredArmLocation,
			FQuat::Identity, ProbeChannel, FCollisionShape::MakeSphere(ProbeSize), Params))
		{
			return DesiredArmLocation;
		}

		UPrimitiveComponent* Component = Hit.GetComponent();
		const bool bIgnoreComponent = ShouldIgnoreCameraComponent(Component, bIgnoreCharacters);
		// Stop on the final query even when pass-through was requested. There may be
		// an unseen wall behind this component; never return the untested far position.
		if (!bIgnoreComponent || Sweep + 1 == SweepLimit)
		{
			LastBlockingComponent = Component;
			bLastSweepBudgetExhausted = Sweep + 1 == SweepLimit && bIgnoreComponent;
			return Hit.Location;
		}
		// Ignore only this component. A sibling may be an explicit camera blocker.
		Params.AddIgnoredComponent(Component);
		++LastIgnoredComponentCount;
	}
	return TraceHitLocation;
}
