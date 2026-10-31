#include "Camera/SWCameraRevealComponent.h"

#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "SceneView.h"
#include "Ship.h"

USWCameraRevealComponent::USWCameraRevealComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	Parameters = TSoftObjectPtr<UMaterialParameterCollection>(FSoftObjectPath(
		TEXT("/Game/Blueprints/Ship/Materials/CameraReveal/MPC_CameraReveal.MPC_CameraReveal")));
}

void USWCameraRevealComponent::BeginPlay()
{
	Super::BeginPlay();
	// Client ownership can be assigned after BeginPlay. Load the shared parameter
	// asset on rendering worlds; UpdateView still permits only the local controller.
	if (GetWorld()->GetNetMode() != NM_DedicatedServer)
	{
		LoadedParameters = Parameters.LoadSynchronous();
	}
}

void USWCameraRevealComponent::ClearView()
{
	Activation = 0.f;
	OcclusionHoldRemaining = 0.f;
	BlockedSampleCount = 0;
	bViewOccluded = false;
	if (LoadedParameters && GetWorld())
	{
		GetWorld()->GetParameterCollectionInstance(LoadedParameters)->SetVectorParameterValue(TEXT("RevealFade"), FLinearColor::Transparent);
	}
}

void USWCameraRevealComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ClearView();
	Super::EndPlay(Reason);
}

bool USWCameraRevealComponent::IsPlayerOccluded(const ABasePlayer* Character, const FVector& CameraLocation)
{
	BlockedSampleCount = 0;
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const FVector Center = Capsule->GetComponentLocation();
	const FVector Up = Capsule->GetUpVector() * Capsule->GetScaledCapsuleHalfHeight();
	const float Heights[] = {-0.15f, 0.25f, 0.7f};
	FCollisionQueryParams Query(SCENE_QUERY_STAT(SWCameraRevealOcclusion), true, Character);
	for (float Height : Heights)
	{
		const FVector Target = Center + Up * Height;
		const FVector Direction = Target - CameraLocation;
		const float Distance = Direction.Size();
		const float Padding = FMath::Clamp(TraceEndPadding, 0.f, 100.f);
		if (Distance <= Padding + 1.f) continue;
		const FVector End = Target - Direction / Distance * Padding;
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, CameraLocation, End, ECC_Camera, Query)) continue;
		// Render meshes are NoCollision. Use the unchanged ship deck/camera query
		// geometry, so fading the surface cannot make the detector oscillate.
		const AActor* Obstacle = Hit.GetActor();
		for (int32 Depth = 0; Obstacle && Depth < 16; ++Depth)
		{
			if (Obstacle->IsA<AShip>())
			{
				++BlockedSampleCount;
				break;
			}
			const AActor* Parent = Obstacle->GetAttachParentActor();
			if (!Parent) Parent = Obstacle->GetOwner();
			if (Parent == Obstacle) break;
			Obstacle = Parent;
		}
	}
	return BlockedSampleCount >= FMath::Clamp(RequiredBlockedSamples, 1, 3);
}

void USWCameraRevealComponent::UpdateView(float DeltaSeconds)
{
	ABasePlayerController* Controller = Cast<ABasePlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !LoadedParameters || !GetWorld()) return;
	ABasePlayer* Character = Controller->GetLifeCharacter();
	ULocalPlayer* LocalPlayer = Controller->GetLocalPlayer();
	const UGameInstance* GameInstance = GetWorld()->GetGameInstance();
	AShip* Ship = Cast<AShip>(Controller->GetPawn());
	if (!Character || !Controller->IsLifeCharacterAlive() || !Controller->PlayerCameraManager
		|| !LocalPlayer || !LocalPlayer->ViewportClient || !LocalPlayer->ViewportClient->Viewport
		|| !GameInstance || GameInstance->GetLocalPlayers().Num() != 1
		|| Ship || Character->IsOperatingCannon())
	{
		ClearView();
		return;
	}
	FSceneViewProjectionData Projection;
	if (!LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, Projection)
		|| !Projection.IsValidViewRectangle() || !Projection.IsPerspectiveProjection())
	{
		ClearView();
		return;
	}
	const FIntRect Rect = Projection.GetConstrainedViewRect();
	const FMatrix ViewProjection = Projection.ComputeViewProjectionMatrix();
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const FVector Center = Capsule->GetComponentLocation();
	const FVector Up = Capsule->GetUpVector() * Capsule->GetScaledCapsuleHalfHeight();
	const FVector Side = Controller->PlayerCameraManager->GetCameraRotation().RotateVector(FVector::RightVector) * Capsule->GetScaledCapsuleRadius();
	FVector2D CenterPixel, Top, Bottom, Left, Right;
	if (!FSceneView::ProjectWorldToScreen(Center, Rect, ViewProjection, CenterPixel)
		|| !FSceneView::ProjectWorldToScreen(Center + Up, Rect, ViewProjection, Top)
		|| !FSceneView::ProjectWorldToScreen(Center - Up, Rect, ViewProjection, Bottom)
		|| !FSceneView::ProjectWorldToScreen(Center - Side, Rect, ViewProjection, Left)
		|| !FSceneView::ProjectWorldToScreen(Center + Side, Rect, ViewProjection, Right))
	{
		ClearView();
		return;
	}
	const float PlayerDepth = ViewProjection.TransformFVector4(FVector4(Center, 1.0)).W;
	const FVector2D UV = (CenterPixel - FVector2D(Rect.Min)) / FVector2D(Rect.Size());
	if (PlayerDepth <= Capsule->GetScaledCapsuleRadius() || UV.X < 0 || UV.X > 1 || UV.Y < 0 || UV.Y > 1)
	{
		ClearView();
		return;
	}
	const float Size = FMath::Max(FVector2D::Distance(Top, Bottom), FVector2D::Distance(Left, Right));
	const float Radius = FixedRadiusScreenHeight > 0.f ? FixedRadiusScreenHeight : Size * FMath::Max(RadiusScale, 0.f) / Rect.Height();
	const bool bCanReveal = bEnabled && VisibilityStrength > 0.f && Radius > 0.f;
	BlockedSampleCount = 0;
	bViewOccluded = bCanReveal && bRequireOcclusion && IsPlayerOccluded(Character, Projection.ViewOrigin);
	if (!bCanReveal || !bRequireOcclusion) OcclusionHoldRemaining = 0.f;
	else if (bViewOccluded) OcclusionHoldRemaining = FMath::Clamp(OcclusionReleaseDelay, 0.f, 2.f);
	else OcclusionHoldRemaining = FMath::Max(0.f, OcclusionHoldRemaining - FMath::Max(DeltaSeconds, 0.f));
	const bool bReveal = bCanReveal && (!bRequireOcclusion || bViewOccluded || OcclusionHoldRemaining > 0.f);
	Activation = TransitionSeconds <= 0.f ? (bReveal ? 1.f : 0.f)
		: FMath::FInterpConstantTo(Activation, bReveal ? 1.f : 0.f, FMath::Max(DeltaSeconds, 0.f), 1.f / FMath::Max(TransitionSeconds, SMALL_NUMBER));
	UMaterialParameterCollectionInstance* Instance = GetWorld()->GetParameterCollectionInstance(LoadedParameters);
	Instance->SetVectorParameterValue(TEXT("RevealCircle"), FLinearColor(UV.X, UV.Y, FMath::Clamp(Radius, 0.f, 8.f), static_cast<float>(Rect.Width()) / Rect.Height()));
	Instance->SetVectorParameterValue(TEXT("RevealFade"), FLinearColor(PlayerDepth + DepthOffset, Activation, FMath::Clamp(VisibilityStrength, 0.f, 1.f), FMath::Clamp(FeatherRatio, 0.f, 1.f)));
	Instance->SetVectorParameterValue(TEXT("RevealDepth"), FLinearColor(FMath::Max(DepthFeather, 0.f), bLimitToPlayerDepth ? 1.f : 0.f, 0.f, 0.f));
}
