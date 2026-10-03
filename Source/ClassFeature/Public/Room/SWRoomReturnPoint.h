#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWRoomReturnPoint.generated.h"

class USceneComponent;
class UInteractableComponent;

UCLASS(Blueprintable)
class CLASSFEATURE_API ASWRoomReturnPoint : public AActor
{
	GENERATED_BODY()
public:
	ASWRoomReturnPoint();
	virtual void BeginPlay() override;
private:
	UFUNCTION() void HandleInteracted(AActor* Interactor);
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInteractableComponent> Interactable;
};
