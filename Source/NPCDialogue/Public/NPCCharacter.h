#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "NPCCharacter.generated.h"

class UInteractableComponent;
class UNPCDialogueSourceComponent;
class USceneComponent;

/** Placeable, animation-ready NPC Pawn with interaction, exclusive dialogue source, and camera anchor. */
UCLASS(Blueprintable)
class NPCDIALOGUE_API ANPCCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ANPCCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintPure, Category = "NPC")
	UInteractableComponent* GetInteractableComponent() const { return InteractableComponent; }

	UFUNCTION(BlueprintPure, Category = "NPC")
	UNPCDialogueSourceComponent* GetDialogueSourceComponent() const { return DialogueSourceComponent; }

	UFUNCTION(BlueprintPure, Category = "NPC")
	USceneComponent* GetDialogueCameraAnchor() const { return DialogueCameraAnchor; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Components")
	TObjectPtr<UInteractableComponent> InteractableComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Components")
	TObjectPtr<UNPCDialogueSourceComponent> DialogueSourceComponent;

	/** Move this component in a child Blueprint to author the dialogue composition. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Components")
	TObjectPtr<USceneComponent> DialogueCameraAnchor;
};
