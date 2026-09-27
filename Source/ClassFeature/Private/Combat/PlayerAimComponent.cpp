#include "Combat/PlayerAimComponent.h"

#include "CollisionChannels.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "SceneView.h"

bool FGameplayAbilityTargetData_ViewRay::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bool bOriginSuccess = false;
	bool bDirectionSuccess = false;
	Origin.NetSerialize(Ar, Map, bOriginSuccess);
	Direction.NetSerialize(Ar, Map, bDirectionSuccess);
	bOutSuccess = bOriginSuccess && bDirectionSuccess && !Ar.IsError();
	return true;
}

UPlayerAimComponent::UPlayerAimComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UPlayerAimComponent::CaptureReleaseView(FGameplayEventData& EventData) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	const ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
	if (!Pawn || !Pawn->IsLocallyControlled() || !LocalPlayer || !LocalPlayer->ViewportClient)
	{
		return false;
	}

	// Use the actual constrained view rectangle, including split screen and letterboxing.
	FSceneViewProjectionData ProjectionData;
	if (!LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, ProjectionData)) return false;
	const FIntRect ViewRect = ProjectionData.GetConstrainedViewRect();
	const FVector2D ScreenCenter = FVector2D(ViewRect.Min) + FVector2D(ViewRect.Size()) * 0.5;
	FVector Origin, Direction;
	if (!Controller->DeprojectScreenPositionToWorld(ScreenCenter.X, ScreenCenter.Y, Origin, Direction)
		|| Origin.ContainsNaN() || Direction.ContainsNaN() || Direction.IsNearlyZero()) return false;

	auto* ViewRay = new FGameplayAbilityTargetData_ViewRay();
	ViewRay->Origin = Origin;
	ViewRay->Direction = Direction.GetSafeNormal();
	EventData.TargetData.Add(ViewRay);
	return true;
}

bool UPlayerAimComponent::ResolveReleaseAim(const FGameplayEventData& EventData, const AActor* Weapon,
	FVector& OutTarget, FVector& OutViewDirection) const
{
	OutTarget = FVector::ZeroVector;
	OutViewDirection = FVector::ZeroVector;
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const FGameplayAbilityTargetData* Data = EventData.TargetData.Num() == 1 ? EventData.TargetData.Get(0) : nullptr;
	if (!Pawn || !GetWorld() || !Data || Data->GetScriptStruct() != FGameplayAbilityTargetData_ViewRay::StaticStruct()) return false;
	const auto* ViewRay = static_cast<const FGameplayAbilityTargetData_ViewRay*>(Data);
	const FVector Origin = ViewRay->Origin;
	const FVector Direction = ViewRay->Direction;
	if (Origin.ContainsNaN() || Direction.ContainsNaN() || !FMath::IsNearlyEqual(Direction.SizeSquared(), 1.0, 0.01)
		|| !FMath::IsFinite(TraceDistance) || TraceDistance < 100.f) return false;

	if (Pawn->HasAuthority())
	{
		if (FVector::DistSquared(Origin, Pawn->GetPawnViewLocation()) > FMath::Square(MaxViewOriginDistance)
			|| FVector::DotProduct(Direction, Pawn->GetBaseAimRotation().Vector())
				< FMath::Cos(FMath::DegreesToRadians(MaxViewAngleDegrees))) return false;
	}

	OutViewDirection = Direction.GetSafeNormal();
	OutTarget = Origin + OutViewDirection * TraceDistance;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PlayerCrosshairAim), false, Pawn);
	if (Weapon) Params.AddIgnoredActor(Weapon);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Origin, OutTarget, ECC_WeaponAim, Params))
	{
		OutTarget = Hit.ImpactPoint;
	}
	return true;
}
