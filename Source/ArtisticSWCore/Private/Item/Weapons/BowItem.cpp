#include "Item/Weapons/BowItem.h"

#include "Components/SkeletalMeshComponent.h"
#include "Item/Components/BowComponent.h"

ABowItem::ABowItem()
{
	// BowMesh is the visible, animated weapon mesh. ItemMesh remains the hidden physics root.
	ItemMesh->SetVisibility(false);
	ItemMesh->SetHiddenInGame(true);

	BowMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BowMesh"));
	BowMesh->SetupAttachment(RootComponent);
	BowMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BowMesh->SetGenerateOverlapEvents(false);

	BowComponent = CreateDefaultSubobject<UBowComponent>(TEXT("BowComponent"));
}

void ABowItem::SetAiming(bool bNewAiming)
{
	if (BowComponent)
	{
		BowComponent->SetAiming(bNewAiming);
	}
}

FTransform ABowItem::GetArrowSpawnTransform() const
{
	if (BowMesh && BowMesh->DoesSocketExist(ArrowSocketName))
	{
		return BowMesh->GetSocketTransform(ArrowSocketName, RTS_World);
	}

	return GetActorTransform();
}

bool ABowItem::GetStringIKTargetTransform(float DrawAlpha, FTransform& OutWorldTransform) const
{
	if (!BowMesh ||
		!BowMesh->DoesSocketExist(StringRestSocketName) ||
		!BowMesh->DoesSocketExist(StringDrawSocketName))
	{
		OutWorldTransform = GetActorTransform();
		return false;
	}

	const FTransform RestTransform = BowMesh->GetSocketTransform(StringRestSocketName, RTS_World);
	const FTransform DrawTransform = BowMesh->GetSocketTransform(StringDrawSocketName, RTS_World);
	const float ClampedAlpha = FMath::Clamp(DrawAlpha, 0.0f, 1.0f);

	OutWorldTransform.Blend(RestTransform, DrawTransform, ClampedAlpha);
	return true;
}

void ABowItem::Multicast_PlayReleaseFX_Implementation()
{
	K2_OnReleaseFX();
}
