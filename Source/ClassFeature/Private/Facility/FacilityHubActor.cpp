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
	}
}

void AFacilityHubActor::HandleInteracted(AActor* Interactor)
{
	if (!HasAuthority() || !Interactor)
	{
		return;
	}

	const ABasePlayer* Player = Cast<ABasePlayer>(Interactor);
	ABasePlayerController* PlayerController = Player
		? Cast<ABasePlayerController>(Player->GetController())
		: nullptr;
	if (PlayerController)
	{
		PlayerController->OpenFacilityHubFromServer(this);
	}
}
