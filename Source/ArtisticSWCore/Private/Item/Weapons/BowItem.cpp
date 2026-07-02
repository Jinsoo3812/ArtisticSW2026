#include "Item/Weapons/BowItem.h"

#include "Components/SkeletalMeshComponent.h"
#include "Item/Components/BowComponent.h"

ABowItem::ABowItem()
{
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

void ABowItem::Multicast_PlayReleaseFX_Implementation()
{
	K2_OnReleaseFX();
}
