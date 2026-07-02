#include "Item/Components/BowComponent.h"

#include "Item/Weapons/BowItem.h"
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
}

void UBowComponent::SetAiming(bool bNewAiming)
{
	if (bIsAiming == bNewAiming)
	{
		return;
	}

	bIsAiming = bNewAiming;
	OnAimStateChanged.Broadcast(bIsAiming);

	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		ServerSetAiming(bNewAiming);
	}
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

	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		ServerSetDrawAlpha(DrawAlpha);
	}
}

float UBowComponent::GetCurrentFireSpeed() const
{
	return FMath::Lerp(MinFireSpeed, MaxFireSpeed, DrawAlpha);
}

FTransform UBowComponent::BuildArrowSpawnTransform() const
{
	if (const ABowItem* Bow = GetOwningBow())
	{
		return Bow->GetArrowSpawnTransform();
	}

	return GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity;
}

void UBowComponent::ServerSetAiming_Implementation(bool bNewAiming)
{
	SetAiming(bNewAiming);
}

void UBowComponent::ServerSetDrawAlpha_Implementation(float NewDrawAlpha)
{
	SetDrawAlpha(NewDrawAlpha);
}

void UBowComponent::OnRep_IsAiming()
{
	OnAimStateChanged.Broadcast(bIsAiming);
}

void UBowComponent::OnRep_DrawAlpha()
{
	OnDrawAlphaChanged.Broadcast(DrawAlpha);
}

ABowItem* UBowComponent::GetOwningBow() const
{
	return Cast<ABowItem>(GetOwner());
}
