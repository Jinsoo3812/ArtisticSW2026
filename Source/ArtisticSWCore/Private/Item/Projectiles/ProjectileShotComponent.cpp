#include "Item/Projectiles/ProjectileShotComponent.h"

#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"

UProjectileShotComponent::UProjectileShotComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// After physics, CMC's PostPhysics based movement, skeletal evaluation and cameras.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

UProjectileShotComponent* UProjectileShotComponent::FindOrAdd(AActor* Shooter)
{
	if (!IsValid(Shooter)) return nullptr;
	if (auto* Existing = Shooter->FindComponentByClass<UProjectileShotComponent>()) return Existing;
	auto* Component = NewObject<UProjectileShotComponent>(Shooter);
	Shooter->AddInstanceComponent(Component);
	Component->RegisterComponent();
	Component->AddTickPrerequisiteActor(Shooter);
	if (const ACharacter* Character = Cast<ACharacter>(Shooter))
	{
		if (Character->GetMesh()) Component->AddTickPrerequisiteComponent(Character->GetMesh());
		if (Character->GetCharacterMovement()) Component->AddTickPrerequisiteComponent(Character->GetCharacterMovement());
	}
	return Component;
}

bool UProjectileShotComponent::Queue(UObject* RequestOwner, const FGuid& ShotId,
	FProjectileShotCommitDelegate Commit, FProjectileShotFinishedDelegate Finished, float Timeout)
{
	if (!IsValid(RequestOwner) || !ShotId.IsValid() || PendingId.IsValid() || !Commit.IsBound()
		|| !GetWorld() || !FMath::IsFinite(Timeout) || Timeout <= 0.0f) return false;
	PendingOwner = RequestOwner;
	PendingId = ShotId;
	CommitDelegate = MoveTemp(Commit);
	FinishedDelegate = MoveTemp(Finished);
	Deadline = GetWorld()->GetTimeSeconds() + Timeout;
	SetComponentTickEnabled(true);
	return true;
}

void UProjectileShotComponent::Cancel(const FGuid& ShotId)
{
	if (PendingId == ShotId) ResetRequest();
}

void UProjectileShotComponent::ResetRequest()
{
	PendingId.Invalidate();
	PendingOwner.Reset();
	CommitDelegate.Unbind();
	FinishedDelegate.Unbind();
	SetComponentTickEnabled(false);
}

void UProjectileShotComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (!PendingOwner.IsValid() || !CommitDelegate.IsBound()) { ResetRequest(); return; }
	const FGuid ExecutingId = PendingId;
	// Callbacks may cancel their request or end the ability, so do not execute the stored delegate in place.
	const FProjectileShotCommitDelegate Commit = CommitDelegate;
	const EProjectileShotCommit Result = GetWorld()->GetTimeSeconds() > Deadline
		? EProjectileShotCommit::Rejected : Commit.Execute();
	if (PendingId != ExecutingId || Result == EProjectileShotCommit::Pending) return;
	const FProjectileShotFinishedDelegate Finished = FinishedDelegate;
	ResetRequest();
	if (Result == EProjectileShotCommit::Rejected)
		UE_LOG(LogTemp, Warning, TEXT("[ProjectileShot] Rejected/timed out Id=%s Shooter=%s"), *ExecutingId.ToString(), *GetNameSafe(GetOwner()));
	Finished.ExecuteIfBound(Result == EProjectileShotCommit::Succeeded);
}

void UProjectileShotComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ResetRequest();
	Super::EndPlay(Reason);
}
