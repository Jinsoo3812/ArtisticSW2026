#include "Room/SWRoomReturnPoint.h"

#include "BasePlayer.h"
#include "Components/SceneComponent.h"
#include "InteractableComponent.h"
#include "Network/SWNetworkLog.h"
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
	UE_LOG(LogSWRoom, Warning,
		TEXT("Flow=ReturnPoint Phase=BeginPlay Actor=%s Parent=%s Authority=%d Interactable=%s Registered=%d Collision=%d Bound=%d"),
		*GetPathName(), *GetNameSafe(GetParentActor()), HasAuthority() ? 1 : 0,
		*GetNameSafe(Interactable), Interactable->IsRegistered() ? 1 : 0,
		Interactable->GetCollisionEnabled() != ECollisionEnabled::NoCollision ? 1 : 0,
		Interactable->OnInteracted.IsBound() ? 1 : 0);
}

void ASWRoomReturnPoint::HandleInteracted(AActor* Interactor)
{
	UE_LOG(LogSWRoom, Warning, TEXT("Flow=ReturnPoint Phase=Interacted Actor=%s Authority=%d Interactor=%s"),
		*GetPathName(), HasAuthority() ? 1 : 0, *GetNameSafe(Interactor));
	if (!HasAuthority())
	{
		UE_LOG(LogSWRoom, Warning, TEXT("Flow=ReturnPoint Result=Ignored Reason=NotAuthority Actor=%s"), *GetPathName());
		return;
	}
	ABasePlayer* Player = Cast<ABasePlayer>(Interactor);
	if (!Player)
	{
		UE_LOG(LogSWRoom, Warning, TEXT("Flow=ReturnPoint Result=Ignored Reason=InteractorNotBasePlayer Actor=%s Interactor=%s"),
			*GetPathName(), *GetNameSafe(Interactor));
		return;
	}
	if (UClassFeatureRoomProgressSubsystem* Room = GetGameInstance()->GetSubsystem<UClassFeatureRoomProgressSubsystem>())
	{
		const bool bAccepted = Room->TryReturn(GetWorld(), Player);
		UE_LOG(LogSWRoom, Warning, TEXT("Flow=ReturnPoint Phase=TryReturn Result=%s Actor=%s Player=%s"),
			bAccepted ? TEXT("Accepted") : TEXT("Rejected"), *GetPathName(), *GetNameSafe(Player));
	}
	else
	{
		UE_LOG(LogSWRoom, Error, TEXT("Flow=ReturnPoint Result=Ignored Reason=RoomSubsystemMissing Actor=%s"), *GetPathName());
	}
}
