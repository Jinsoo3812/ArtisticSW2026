#include "WeaponFeedback/WeaponFeedbackComponent.h"

#include "Components/SceneComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "WeaponFeedback/WeaponFeedbackDataAsset.h"

UWeaponFeedbackComponent::UWeaponFeedbackComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UWeaponFeedbackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ForceStopWeaponTrail(true);
	Super::EndPlay(EndPlayReason);
}

void UWeaponFeedbackComponent::SetFeedbackData(UWeaponFeedbackDataAsset* InFeedbackData)
{
	if (FeedbackData == InFeedbackData)
	{
		return;
	}

	ForceStopWeaponTrail(true);
	FeedbackData = InFeedbackData;
}

bool UWeaponFeedbackComponent::PlaySwingSound(FName SoundSetName, float VolumeMultiplier, float PitchMultiplier)
{
	if (!ShouldRunCosmetics() || !FeedbackData)
	{
		return false;
	}

	const FWeaponSwingSoundSet* SoundSet = FeedbackData->FindSwingSoundSet(SoundSetName);
	USceneComponent* AttachComponent = ResolveAttachmentComponent();
	USoundBase* Sound = SoundSet ? SoundSet->ChooseSound() : nullptr;
	if (!SoundSet || !Sound || !AttachComponent)
	{
		return false;
	}

	return UGameplayStatics::SpawnSoundAttached(
		Sound,
		AttachComponent,
		NAME_None,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset,
		true,
		FMath::Max(0.0f, SoundSet->VolumeMultiplier * VolumeMultiplier),
		FMath::Max(0.01f, SoundSet->ChoosePitchMultiplier() * PitchMultiplier),
		0.0f,
		SoundSet->AttenuationSettings,
		SoundSet->ConcurrencySettings,
		true) != nullptr;
}

bool UWeaponFeedbackComponent::BeginWeaponTrail()
{
	if (!ShouldRunCosmetics() || !FeedbackData)
	{
		return false;
	}

	const FWeaponTrailFeedback& Trail = FeedbackData->GetTrailFeedback();
	USceneComponent* AttachComponent = ResolveAttachmentComponent();
	if (!Trail.IsConfigured() || !AttachComponent)
	{
		return false;
	}

	FVector StartLocation = FVector::ZeroVector;
	FVector EndLocation = FVector::ZeroVector;
	if (Trail.UsesEndpointParameters() && !ResolveTrailLocations(StartLocation, EndLocation))
	{
		return false;
	}

	++TrailRequestCount;
	if (IsValid(ActiveTrailComponent.Get()))
	{
		UpdateWeaponTrail();
		return true;
	}

	ActiveTrailComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
		Trail.NiagaraSystem,
		AttachComponent,
		Trail.AttachSocketName,
		Trail.RelativeLocation,
		Trail.RelativeRotation,
		Trail.ComponentScale,
		EAttachLocation::KeepRelativeOffset,
		true,
		ENCPoolMethod::None,
		false,
		true);

	if (!ActiveTrailComponent)
	{
		TrailRequestCount = FMath::Max(0, TrailRequestCount - 1);
		return false;
	}

	if (Trail.UsesEndpointParameters())
	{
		ActiveTrailComponent->SetVariableVec3(Trail.StartPositionParameter, StartLocation);
		ActiveTrailComponent->SetVariableVec3(Trail.EndPositionParameter, EndLocation);
	}
	ActiveTrailComponent->Activate(true);
	return true;
}

void UWeaponFeedbackComponent::UpdateWeaponTrail()
{
	if (!ShouldRunCosmetics() || !FeedbackData || !IsValid(ActiveTrailComponent.Get()))
	{
		return;
	}

	const FWeaponTrailFeedback& Trail = FeedbackData->GetTrailFeedback();
	if (!Trail.UsesEndpointParameters())
	{
		return;
	}

	FVector StartLocation;
	FVector EndLocation;
	if (!ResolveTrailLocations(StartLocation, EndLocation))
	{
		ForceStopWeaponTrail(true);
		return;
	}

	ActiveTrailComponent->SetVariableVec3(Trail.StartPositionParameter, StartLocation);
	ActiveTrailComponent->SetVariableVec3(Trail.EndPositionParameter, EndLocation);
}

void UWeaponFeedbackComponent::EndWeaponTrail()
{
	TrailRequestCount = FMath::Max(0, TrailRequestCount - 1);
	if (TrailRequestCount > 0)
	{
		return;
	}

	if (IsValid(ActiveTrailComponent.Get()))
	{
		ActiveTrailComponent->Deactivate();
	}
	ActiveTrailComponent = nullptr;
}

void UWeaponFeedbackComponent::ForceStopWeaponTrail(bool bImmediate)
{
	TrailRequestCount = 0;
	if (!IsValid(ActiveTrailComponent.Get()))
	{
		ActiveTrailComponent = nullptr;
		return;
	}

	if (bImmediate)
	{
		ActiveTrailComponent->DeactivateImmediate();
		ActiveTrailComponent->DestroyComponent();
	}
	else
	{
		ActiveTrailComponent->Deactivate();
	}
	ActiveTrailComponent = nullptr;
}

void UWeaponFeedbackComponent::SetTrailEndpointComponents(
	USceneComponent* InStartComponent,
	USceneComponent* InEndComponent)
{
	TrailStartComponent = InStartComponent;
	TrailEndComponent = InEndComponent;
}

USceneComponent* UWeaponFeedbackComponent::ResolveAttachmentComponent() const
{
	const AActor* OwnerActor = GetOwner();
	return OwnerActor ? OwnerActor->GetRootComponent() : nullptr;
}

bool UWeaponFeedbackComponent::ResolveTrailLocations(FVector& OutStartLocation, FVector& OutEndLocation) const
{
	if (!FeedbackData)
	{
		return false;
	}

	if (IsValid(TrailStartComponent.Get()) && IsValid(TrailEndComponent.Get()))
	{
		OutStartLocation = TrailStartComponent->GetComponentLocation();
		OutEndLocation = TrailEndComponent->GetComponentLocation();
		return true;
	}

	USceneComponent* AttachComponent = ResolveAttachmentComponent();
	const FWeaponTrailFeedback& Trail = FeedbackData->GetTrailFeedback();
	if (!AttachComponent
		|| Trail.StartSocketName.IsNone()
		|| Trail.EndSocketName.IsNone()
		|| !AttachComponent->DoesSocketExist(Trail.StartSocketName)
		|| !AttachComponent->DoesSocketExist(Trail.EndSocketName))
	{
		return false;
	}

	OutStartLocation = AttachComponent->GetSocketLocation(Trail.StartSocketName);
	OutEndLocation = AttachComponent->GetSocketLocation(Trail.EndSocketName);
	return true;
}

bool UWeaponFeedbackComponent::ShouldRunCosmetics() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_DedicatedServer;
}
