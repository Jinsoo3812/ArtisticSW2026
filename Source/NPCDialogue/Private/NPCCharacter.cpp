#include "NPCCharacter.h"

#include "BaseGameplayTags.h"
#include "InteractableComponent.h"
#include "NPCDialogueData.h"
#include "NPCDialogueSourceComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"

ANPCCharacter::ANPCCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicates = true;
	SetReplicateMovement(true);

	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));
	InteractableComponent->SetupAttachment(GetCapsuleComponent());
	InteractableComponent->InteractionTag = Interaction_Dialogue;

	DialogueSourceComponent = CreateDefaultSubobject<UNPCDialogueSourceComponent>(TEXT("DialogueSourceComponent"));

	DialogueCameraAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("DialogueCameraAnchor"));
	DialogueCameraAnchor->SetupAttachment(GetRootComponent());
	DialogueCameraAnchor->SetRelativeLocation(FVector(180.0, 140.0, 145.0));
	DialogueCameraAnchor->ComponentTags.Add(TEXT("DialogueCamera"));
}

void ANPCCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (InteractableComponent && DialogueSourceComponent)
	{
		if (const UNPCDialogueData* Data = DialogueSourceComponent->GetDialogueData())
		{
			InteractableComponent->InitializeInteractable(Data->DisplayName, Data->InteractionActionText);
		}
	}
}
