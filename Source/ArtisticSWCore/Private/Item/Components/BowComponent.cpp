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
