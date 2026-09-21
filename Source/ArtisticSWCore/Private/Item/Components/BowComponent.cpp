#include "Item/Components/BowComponent.h"

#include "Item/Weapons/BowItem.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

UBowComponent::UBowComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UBowComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UBowComponent, bIsAiming);
	DOREPLIFETIME(UBowComponent, DrawAlpha);
	DOREPLIFETIME(UBowComponent, bArrowNocked);
}

void UBowComponent::SetAiming(bool bNewAiming)
{
	if (bIsAiming == bNewAiming)
	{
		return;
	}

	bIsAiming = bNewAiming;
	OnAimStateChanged.Broadcast(bIsAiming);
}

void UBowComponent::SetDrawAlpha(float NewDrawAlpha)
{
	const float ClampedDrawAlpha = FMath::Clamp(NewDrawAlpha, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(DrawAlpha, ClampedDrawAlpha))
	{
		return;
	}

	DrawAlpha = ClampedDrawAlpha;
	OnDrawAlphaChanged.Broadcast(DrawAlpha);
}

float UBowComponent::GetFireSpeed(float ReleaseDrawAlpha) const
{
	if (!FMath::IsFinite(ReleaseDrawAlpha) || !FMath::IsFinite(MinFireSpeed)
		|| !FMath::IsFinite(MaxFireSpeed) || MinFireSpeed <= 0.f || MaxFireSpeed < MinFireSpeed)
	{
		return 0.f;
	}
	return FMath::Lerp(MinFireSpeed, MaxFireSpeed, FMath::Clamp(ReleaseDrawAlpha, 0.f, 1.f));
}

bool UBowComponent::TryBuildArrowLaunch(float FireSpeed, const FVector& AimTarget, const FVector& ViewDirection,
	FTransform& OutSpawnTransform, FVector& OutLaunchVelocity) const
{
	OutSpawnTransform = FTransform::Identity;
	OutLaunchVelocity = FVector::ZeroVector;
	const ABowItem* Bow = GetOwningBow();
	if (!FMath::IsFinite(FireSpeed) || FireSpeed <= 0.f || !Bow
		|| AimTarget.ContainsNaN() || ViewDirection.ContainsNaN() || ViewDirection.IsNearlyZero()
		|| !Bow->TryGetArrowSpawnTransform(OutSpawnTransform) || OutSpawnTransform.ContainsNaN())
	{
		return false;
	}

	// Camera hits behind/too close to the socket must never turn the arrow backward.
	const FVector Forward = ViewDirection.GetSafeNormal();
	const FVector ToTarget = AimTarget - OutSpawnTransform.GetLocation();
	const FVector LaunchDirection = FVector::DotProduct(ToTarget, Forward) > 10.f
		? ToTarget.GetSafeNormal() : Forward;
	OutLaunchVelocity = LaunchDirection * FireSpeed;
	OutSpawnTransform.SetRotation(LaunchDirection.Rotation().Quaternion());
	OutSpawnTransform.SetScale3D(FVector::OneVector);
	return !OutLaunchVelocity.ContainsNaN() && !OutLaunchVelocity.IsNearlyZero();
}

void UBowComponent::SetArrowNocked(bool bNewArrowNocked)
{
	AActor* BowActor = GetOwner();
	if (!BowActor)
	{
		return;
	}

	if (!BowActor->HasAuthority())
	{
		if (!IsLocallyControlledOwner())
		{
			return;
		}

		bPredictedArrowNocked = bNewArrowNocked;
		ApplyArrowNockedPresentation();
		return;
	}

	if (bArrowNocked == bNewArrowNocked)
	{
		ApplyArrowNockedPresentation();
		return;
	}

	bArrowNocked = bNewArrowNocked;
	ApplyArrowNockedPresentation();

	// The montage notify executes on both the owning client and authority. Do not add an
	// item-owned RPC: legacy inventory items do not consistently establish a controller chain.
	BowActor->ForceNetUpdate();
}

bool UBowComponent::IsArrowNocked() const
{
	const AActor* BowActor = GetOwner();
	return BowActor && !BowActor->HasAuthority() && IsLocallyControlledOwner()
		? bPredictedArrowNocked
		: bArrowNocked;
}

void UBowComponent::OnRep_IsAiming()
{
	OnAimStateChanged.Broadcast(bIsAiming);
}

void UBowComponent::OnRep_DrawAlpha()
{
	OnDrawAlphaChanged.Broadcast(DrawAlpha);
}

void UBowComponent::OnRep_ArrowNocked()
{
	ApplyArrowNockedPresentation();
}

void UBowComponent::ApplyArrowNockedPresentation()
{
	if (ABowItem* Bow = GetOwningBow())
	{
		Bow->SetNockedArrowVisible(IsArrowNocked());
	}
}

bool UBowComponent::IsLocallyControlledOwner() const
{
	const AActor* BowActor = GetOwner();
	const APawn* OwningPawn = BowActor ? Cast<APawn>(BowActor->GetOwner()) : nullptr;
	if (!OwningPawn && BowActor)
	{
		OwningPawn = Cast<APawn>(BowActor->GetAttachParentActor());
	}
	return OwningPawn && OwningPawn->IsLocallyControlled();
}

ABowItem* UBowComponent::GetOwningBow() const
{
	return Cast<ABowItem>(GetOwner());
}
