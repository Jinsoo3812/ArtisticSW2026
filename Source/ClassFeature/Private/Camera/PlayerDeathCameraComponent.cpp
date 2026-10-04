#include "Camera/PlayerDeathCameraComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "PhysicsEngine/BodyInstance.h"

UPlayerDeathCameraComponent::UPlayerDeathCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UPlayerDeathCameraComponent::StartFollowing(USpringArmComponent* InCameraBoom, USkeletalMeshComponent* InMesh)
{
	if (bFollowing || !IsValid(InCameraBoom) || !IsValid(InMesh)
		|| !GetOwner() || GetOwner()->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	CameraBoom = InCameraBoom;
	Mesh = InMesh;
	InitialBoomRelativeTransform = InCameraBoom->GetRelativeTransform();
	InitialBoomTickGroup = InCameraBoom->PrimaryComponentTick.TickGroup;
	// Read the solved body first, then let SpringArm calculate collision and its camera socket.
	InCameraBoom->SetTickGroup(TG_PostPhysics);
	InCameraBoom->AddTickPrerequisiteComponent(this);
	bFollowing = true;
	SetComponentTickEnabled(true);
}

FVector UPlayerDeathCameraComponent::GetFocusLocation() const
{
	const USkeletalMeshComponent* MeshComponent = Mesh.Get();
	if (!MeshComponent) return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	// Read Chaos directly: animation bone refresh can be skipped when the corpse is off screen.
	if (FBodyInstance* Body = MeshComponent->GetBodyInstance(FocusBone); Body && Body->IsValidBodyInstance())
	{
		return Body->GetUnrealWorldTransform().GetLocation();
	}
	if (MeshComponent->GetBoneIndex(FocusBone) != INDEX_NONE)
	{
		return MeshComponent->GetBoneLocation(FocusBone);
	}
	return MeshComponent->GetComponentLocation();
}

void UPlayerDeathCameraComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	USpringArmComponent* Boom = CameraBoom.Get();
	if (!bFollowing || !Boom || !Mesh.IsValid())
	{
		StopFollowing();
		return;
	}
	const FVector Target = GetFocusLocation() + FocusOffset;
	const FVector Location = FollowInterpSpeed > 0.0f
		? FMath::VInterpTo(Boom->GetComponentLocation(), Target, DeltaTime, FollowInterpSpeed) : Target;
	// Translate only the camera pivot. Ragdoll rotation must not roll the player's camera.
	Boom->SetWorldLocation(Location);
}

void UPlayerDeathCameraComponent::StopFollowing()
{
	if (bFollowing)
	{
		if (USpringArmComponent* Boom = CameraBoom.Get())
		{
			Boom->RemoveTickPrerequisiteComponent(this);
			Boom->SetTickGroup(InitialBoomTickGroup);
			Boom->SetRelativeTransform(InitialBoomRelativeTransform);
		}
	}
	bFollowing = false;
	SetComponentTickEnabled(false);
	CameraBoom.Reset();
	Mesh.Reset();
}

void UPlayerDeathCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopFollowing();
	Super::EndPlay(EndPlayReason);
}
