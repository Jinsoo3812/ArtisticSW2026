#include "Facility/FacilityHubActor.h"

#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "InteractableComponent.h"

void AFacilityHubActor::BeginPlay()
{
	Super::BeginPlay();

	if (UInteractableComponent* Interactable = FindComponentByClass<UInteractableComponent>())
	{
		Interactable->OnInteracted.AddUniqueDynamic(this, &AFacilityHubActor::HandleInteracted);
		UE_LOG(LogTemp, Log,
			TEXT("[FacilityHubFlow][SERVER] Ready. Facility=%s Interactable=%s"),
			*GetNameSafe(this),
			*GetNameSafe(Interactable));
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[FacilityHubFlow][SERVER] FAILED: Facility has no InteractableComponent. Facility=%s"),
			*GetNameSafe(this));
	}
}

void AFacilityHubActor::HandleInteracted(AActor* Interactor)
{
	UE_LOG(LogTemp, Log,
		TEXT("[FacilityHubFlow][SERVER] Interaction received. Facility=%s Interactor=%s Authority=%s"),
		*GetNameSafe(this),
		*GetNameSafe(Interactor),
		HasAuthority() ? TEXT("YES") : TEXT("NO"));

	if (!HasAuthority() || !Interactor)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[FacilityHubFlow] Interaction rejected before controller lookup. Authority=%s InteractorValid=%s"),
			HasAuthority() ? TEXT("YES") : TEXT("NO"),
			IsValid(Interactor) ? TEXT("YES") : TEXT("NO"));
		return;
	}

	const ABasePlayer* Player = Cast<ABasePlayer>(Interactor);
	ABasePlayerController* PlayerController = Player
		? Cast<ABasePlayerController>(Player->GetController())
		: nullptr;
	if (PlayerController)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[FacilityHubFlow][SERVER] Opening common FacilityHub. Controller=%s Context=%s"),
			*GetNameSafe(PlayerController),
			*GetNameSafe(this));
		PlayerController->OpenFacilityHubFromServer(this);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[FacilityHubFlow][SERVER] FAILED: BasePlayerController not found. InteractorClass=%s"),
			*GetNameSafe(Interactor->GetClass()));
	}
}
