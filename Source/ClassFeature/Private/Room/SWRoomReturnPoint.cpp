#include "Room/SWRoomReturnPoint.h"

#include "BasePlayer.h"
#include "Components/SceneComponent.h"
#include "InteractableComponent.h"
#include "Room/ClassFeatureRoomProgressSubsystem.h"

ASWRoomReturnPoint::ASWRoomReturnPoint()
{
	bReplicates = true;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
	Interactable = CreateDefaultSubobject<UInteractableComponent>(TEXT("Interactable"));
	Interactable->SetupAttachment(SceneRoot);
	Interactable->InitializeInteractable(FText::FromString(TEXT("귀환")), FText::FromString(TEXT("귀환하기")));
}

void ASWRoomReturnPoint::BeginPlay()
{
	Super::BeginPlay();
	Interactable->OnInteracted.AddDynamic(this, &ASWRoomReturnPoint::HandleInteracted);
}

void ASWRoomReturnPoint::HandleInteracted(AActor* Interactor)
{
	if (!HasAuthority()) return;
	ABasePlayer* Player = Cast<ABasePlayer>(Interactor);
	if (!Player) return;
	if (UClassFeatureRoomProgressSubsystem* Room = GetGameInstance()->GetSubsystem<UClassFeatureRoomProgressSubsystem>())
		Room->TryReturn(GetWorld(), Player);
}
