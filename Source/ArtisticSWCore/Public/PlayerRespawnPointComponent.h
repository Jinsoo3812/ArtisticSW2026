#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "PlayerRespawnTypes.h"
#include "PlayerRespawnPointComponent.generated.h"

/** A ship-attached spawn transform. Add two components and assign Player0/Player1. */
UCLASS(ClassGroup=(Respawn), BlueprintType, Blueprintable, meta=(BlueprintSpawnableComponent))
class ARTISTICSWCORE_API UPlayerRespawnPointComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Respawn")
	ESWPlayerSlot PlayerSlot = ESWPlayerSlot::Any;
};
