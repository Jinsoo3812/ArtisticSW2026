#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlayerRespawnTypes.h"
#include "PlayerRespawnPoint.generated.h"

class UArrowComponent;

/** Level-placed respawn point used after the whole level is reloaded. */
UCLASS(Blueprintable)
class ARTISTICSWCORE_API APlayerRespawnPoint : public AActor
{
	GENERATED_BODY()

public:
	APlayerRespawnPoint();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Respawn")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Respawn")
	TObjectPtr<UArrowComponent> Arrow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Respawn")
	ESWPlayerSlot PlayerSlot = ESWPlayerSlot::Any;
};
