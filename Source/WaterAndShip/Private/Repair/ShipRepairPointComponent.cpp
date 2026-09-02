#include "Repair/ShipRepairPointComponent.h"

#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Ship.h"
#include "ShipRepairUserInterface.h"

UShipRepairPointComponent::UShipRepairPointComponent()
{
	SetIsReplicatedByDefault(true);
	InitSphereRadius(85.0f);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InitializeInteractable(
		NSLOCTEXT("ShipRepair", "LeakName", "Hull Leak"),
		NSLOCTEXT("ShipRepair", "LeakAction", "Hold F to repair"));
}

void UShipRepairPointComponent::BeginPlay()
{
	Super::BeginPlay();
	OnInteracted.AddUniqueDynamic(this, &UShipRepairPointComponent::HandleInteracted);
	UpdateVisualState();
}

void UShipRepairPointComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UShipRepairPointComponent, bLeakActive);
}

void UShipRepairPointComponent::ActivateLeak()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bLeakActive)
	{
		return;
	}
	bLeakActive = true;
	UpdateVisualState();
	GetOwner()->ForceNetUpdate();
}

void UShipRepairPointComponent::DeactivateLeak()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bLeakActive)
	{
		return;
	}
	ClearRepair(false);
	bLeakActive = false;
	UpdateVisualState();
	GetOwner()->ForceNetUpdate();
}

void UShipRepairPointComponent::HandleInteracted(AActor* Interactor)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bLeakActive || RepairingActor.IsValid() || !Interactor)
	{
		return;
	}

	IShipRepairUserInterface* RepairUser = Cast<IShipRepairUserInterface>(Interactor);
	AShip* Ship = Cast<AShip>(GetOwner());
	FGameplayTag MaterialTag;
	float RepairAmount = 0.0f;
	if (!RepairUser || !Ship || !RepairUser->GetEquippedShipRepairMaterial(MaterialTag)
		|| !Ship->ResolveRepairMaterial(MaterialTag, RepairAmount))
	{
		return;
	}

	RepairingActor = Interactor;
	PendingMaterialTag = MaterialTag;
	RepairUser->BeginShipRepair(this, RepairDuration);
	GetWorld()->GetTimerManager().SetTimer(
		RepairCompletionTimer, this, &UShipRepairPointComponent::CompleteRepair, RepairDuration, false);
	GetWorld()->GetTimerManager().SetTimer(
		RepairValidationTimer, this, &UShipRepairPointComponent::ValidateRepair, 0.1f, true);
}

void UShipRepairPointComponent::ValidateRepair()
{
	AActor* Interactor = RepairingActor.Get();
	IShipRepairUserInterface* RepairUser = Cast<IShipRepairUserInterface>(Interactor);
	FGameplayTag CurrentMaterial;
	const bool bMaterialStillHeld = RepairUser && RepairUser->IsShipRepairInputHeld()
		&& RepairUser->GetEquippedShipRepairMaterial(CurrentMaterial)
		&& CurrentMaterial.MatchesTagExact(PendingMaterialTag);
	const bool bInRange = Interactor
		&& FVector::DistSquared(Interactor->GetActorLocation(), GetComponentLocation())
			<= FMath::Square(RepairCancelDistance);
	if (!bLeakActive || !bMaterialStillHeld || !bInRange)
	{
		CancelRepair(Interactor);
	}
}

void UShipRepairPointComponent::CompleteRepair()
{
	AActor* Interactor = RepairingActor.Get();
	IShipRepairUserInterface* RepairUser = Cast<IShipRepairUserInterface>(Interactor);
	AShip* Ship = Cast<AShip>(GetOwner());
	float RepairAmount = 0.0f;
	if (!bLeakActive || !RepairUser || !RepairUser->IsShipRepairInputHeld() || !Ship
		|| !Ship->ResolveRepairMaterial(PendingMaterialTag, RepairAmount)
		|| !RepairUser->ConsumeShipRepairMaterial(PendingMaterialTag))
	{
		ClearRepair(false);
		return;
	}

	ClearRepair(true);
	Ship->CompleteRepairPoint(this, RepairAmount);
}

void UShipRepairPointComponent::CancelRepair(AActor* RequestingActor)
{
	if (!RepairingActor.IsValid() || (RequestingActor && RequestingActor != RepairingActor.Get()))
	{
		return;
	}
	ClearRepair(false);
}

void UShipRepairPointComponent::ClearRepair(bool bCompleted)
{
	AActor* Interactor = RepairingActor.Get();
	GetWorld()->GetTimerManager().ClearTimer(RepairCompletionTimer);
	GetWorld()->GetTimerManager().ClearTimer(RepairValidationTimer);
	RepairingActor.Reset();
	PendingMaterialTag = FGameplayTag();
	if (IShipRepairUserInterface* RepairUser = Cast<IShipRepairUserInterface>(Interactor))
	{
		RepairUser->EndShipRepair(this, bCompleted);
	}
}

void UShipRepairPointComponent::OnRep_LeakActive()
{
	UpdateVisualState();
}

void UShipRepairPointComponent::UpdateVisualState()
{
	SetCollisionEnabled(bLeakActive ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	if (bLeakActive && LeakNiagaraSystem && !IsValid(ActiveLeakComponent))
	{
		ActiveLeakComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			LeakNiagaraSystem, this, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, false, true, ENCPoolMethod::AutoRelease, true);
	}
	else if (!bLeakActive && IsValid(ActiveLeakComponent))
	{
		ActiveLeakComponent->DeactivateImmediate();
		ActiveLeakComponent = nullptr;
	}
}
