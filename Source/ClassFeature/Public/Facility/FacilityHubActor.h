#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FacilityHubActor.generated.h"

class ABasePlayerController;
class APlayerState;

/**
 * Runtime parent for the integrated facility Blueprint.
 * The Blueprint owns its mesh, interaction volume, and feature access components.
 */
UCLASS()
class CLASSFEATURE_API AFacilityHubActor : public AActor
{
	GENERATED_BODY()

public:
	AFacilityHubActor();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server-authoritative exclusive access for the integrated facility UI. */
	bool TryAcquire(ABasePlayerController* PlayerController);
	void Release(ABasePlayerController* PlayerController);
	bool IsOccupiedBy(const ABasePlayerController* PlayerController) const;

	UFUNCTION(BlueprintPure, Category = "Facility Hub")
	bool IsOccupied() const { return CurrentUser != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Facility Hub")
	APlayerState* GetCurrentUser() const { return CurrentUser.Get(); }

private:
	UFUNCTION()
	void HandleInteracted(AActor* Interactor);

	UPROPERTY(Replicated)
	TObjectPtr<APlayerState> CurrentUser;
};
