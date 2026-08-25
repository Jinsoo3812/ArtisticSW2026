#include "Item/Weapons/BowItem.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Item/Components/BowComponent.h"
#include "Item/Projectiles/ArrowProjectile.h"

ABowItem::ABowItem()
{
	// BowMesh is the visible, animated weapon mesh. ItemMesh remains the hidden physics root.
	ItemMesh->SetVisibility(false);
	ItemMesh->SetHiddenInGame(true);

	BowMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BowMesh"));
	BowMesh->SetupAttachment(RootComponent);
	BowMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BowMesh->SetGenerateOverlapEvents(false);

	NockedArrowMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NockedArrowMesh"));
	NockedArrowMesh->SetupAttachment(BowMesh);
	NockedArrowMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NockedArrowMesh->SetGenerateOverlapEvents(false);
	NockedArrowMesh->SetHiddenInGame(true);
	NockedArrowMesh->SetVisibility(false, true);

	BowComponent = CreateDefaultSubobject<UBowComponent>(TEXT("BowComponent"));
}

void ABowItem::BeginPlay()
{
	OnItemInitialized.AddUObject(this, &ABowItem::HandleItemInitialized);
	Super::BeginPlay();
	RefreshNockedArrowVisual();
	SetNockedArrowVisible(BowComponent && BowComponent->IsArrowNocked());
}

void ABowItem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnItemInitialized.RemoveAll(this);
	Super::EndPlay(EndPlayReason);
}

void ABowItem::HandleItemInitialized(ABaseItem* InitializedItem)
{
	if (InitializedItem == this)
	{
		RefreshNockedArrowVisual();
	}
}

bool ABowItem::RefreshNockedArrowVisual()
{
	if (!NockedArrowMesh)
	{
		return false;
	}

	const TSubclassOf<AActor> SpawnClass = GetSpawnClass();
	const AArrowProjectile* ArrowCDO = SpawnClass
		? Cast<AArrowProjectile>(SpawnClass->GetDefaultObject())
		: nullptr;
	return ArrowCDO && ArrowCDO->ApplyVisualTo(NockedArrowMesh);
}

void ABowItem::SetAiming(bool bNewAiming)
{
	if (BowComponent)
	{
		BowComponent->SetAiming(bNewAiming);
	}
}

bool ABowItem::BindArrowAnchor(USkeletalMeshComponent* CharacterMesh)
{
	if (!CharacterMesh || CharacterArrowSocketName.IsNone()
		|| !CharacterMesh->DoesSocketExist(CharacterArrowSocketName))
	{
		UnbindArrowAnchor();
		return false;
	}

	ArrowAnchorMesh = CharacterMesh;
	if (BowComponent)
	{
		SetNockedArrowVisible(BowComponent->IsArrowNocked());
	}
	return true;
}

void ABowItem::UnbindArrowAnchor()
{
	SetNockedArrowVisible(false);
	ArrowAnchorMesh.Reset();

	if (NockedArrowMesh && BowMesh && NockedArrowMesh->GetAttachParent() != BowMesh)
	{
		NockedArrowMesh->AttachToComponent(
			BowMesh,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
}

bool ABowItem::TryGetArrowSpawnTransform(FTransform& OutSpawnTransform) const
{
	OutSpawnTransform = FTransform::Identity;
	const USkeletalMeshComponent* CharacterMesh = ArrowAnchorMesh.Get();
	if (!CharacterMesh || CharacterArrowSocketName.IsNone()
		|| !CharacterMesh->DoesSocketExist(CharacterArrowSocketName))
	{
		return false;
	}

	OutSpawnTransform = CharacterMesh->GetSocketTransform(CharacterArrowSocketName, RTS_World);
	return true;
}

bool ABowItem::SetNockedArrowVisible(bool bVisible)
{
	if (!NockedArrowMesh)
	{
		return false;
	}

	USkeletalMeshComponent* CharacterMesh = ArrowAnchorMesh.Get();
	const bool bHasValidAnchor = CharacterMesh
		&& !CharacterArrowSocketName.IsNone()
		&& CharacterMesh->DoesSocketExist(CharacterArrowSocketName);
	bool bCanShow = false;
	if (bVisible && bHasValidAnchor)
	{
		const bool bAttached = NockedArrowMesh->AttachToComponent(
			CharacterMesh,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			CharacterArrowSocketName);
		bCanShow = bAttached && RefreshNockedArrowVisual();
	}

	NockedArrowMesh->SetVisibility(bCanShow, true);
	NockedArrowMesh->SetHiddenInGame(!bCanShow, true);
	return !bVisible || bCanShow;
}

bool ABowItem::IsNockedArrowVisible() const
{
	return NockedArrowMesh && NockedArrowMesh->IsVisible() && !NockedArrowMesh->bHiddenInGame;
}

USceneComponent* ABowItem::GetAttachmentReferenceComponent() const
{
	return BowMesh;
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

bool ABowItem::GetCharacterStringGripTargetTransform(FTransform& OutBowComponentSpaceTransform) const
{
	OutBowComponentSpaceTransform = FTransform::Identity;

	if (!BowMesh || CharacterStringGripSocketName.IsNone())
	{
		return false;
	}

	const USceneComponent* ItemAttachmentParent = GetRootComponent() ? GetRootComponent()->GetAttachParent() : nullptr;
	const USkeletalMeshComponent* CharacterMesh = Cast<USkeletalMeshComponent>(ItemAttachmentParent);
	if (!CharacterMesh || !CharacterMesh->DoesSocketExist(CharacterStringGripSocketName))
	{
		return false;
	}

	const FTransform GripWorldTransform = CharacterMesh->GetSocketTransform(CharacterStringGripSocketName, RTS_World);
	OutBowComponentSpaceTransform = GripWorldTransform.GetRelativeTransform(BowMesh->GetComponentTransform());
	return true;
}

void ABowItem::Multicast_PlayReleaseFX_Implementation()
{
	K2_OnReleaseFX();
}
