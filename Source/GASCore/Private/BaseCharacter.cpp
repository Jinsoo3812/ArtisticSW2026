// Fill out your copyright notice in the Description page of Project Settings.


#include "BaseCharacter.h"

#include "Components/BaseHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

ABaseCharacter::ABaseCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(35.f, 90.f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 500.f, 0.f);

	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 0.0f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
}

void ABaseCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (const USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		InitialMeshRelativeTransform = MeshComponent->GetRelativeTransform();
		InitialMeshCollisionProfileName = MeshComponent->GetCollisionProfileName();
		InitialMeshCollisionEnabled = MeshComponent->GetCollisionEnabled();
	}
}

void ABaseCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

UAbilitySystemComponent* ABaseCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ABaseCharacter::ApplyLocalDeathRagdoll()
{
	if (bLocalDeathRagdollApplied)
	{
		return;
	}

	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return;
	}

	bLocalDeathRagdollApplied = true;

	if (bDetachControllerOnDeathRagdoll)
	{
		DetachFromControllerPendingDestroy();
	}

	if (bDisableMovementOnDeathRagdoll)
	{
		if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
		{
			MovementComponent->DisableMovement();
			MovementComponent->StopMovementImmediately();
		}
	}

	if (bDisableCapsuleCollisionOnDeathRagdoll)
	{
		if (UCapsuleComponent* Capsule = GetCapsuleComponent())
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	// Ragdoll changes the mesh from a Pawn presentation component into a
	// PhysicsBody. This must match ShipDeck's PhysicsBody response on all peers.
	MeshComponent->SetCollisionProfileName(TEXT("Ragdoll"));
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComponent->SetCollisionObjectType(ECC_PhysicsBody);
	MeshComponent->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	MeshComponent->SetAllUseCCD(bUseDeathRagdollCCD);
	MeshComponent->SetAllBodiesSimulatePhysics(true);
	MeshComponent->SetSimulatePhysics(true);
	MeshComponent->WakeAllRigidBodies();

	if (bApplyDeathRagdollImpulse)
	{
		FDeathRagdollImpactData ImpactData;
		if (const UBaseHealthComponent* HealthComponent = FindComponentByClass<UBaseHealthComponent>())
		{
			ImpactData = HealthComponent->GetDeathRagdollImpactData();
		}

		// A death with no lethal-hit direction simply enters ragdoll. Do not fall
		// back to the actor's facing direction; that was the legacy fixed knockback.
		if (ImpactData.bHasDirection)
		{
			const FVector KnockbackDirection =
				FVector(ImpactData.KnockbackDirection).GetSafeNormal2D();
			const FVector Impulse =
				KnockbackDirection * DeathRagdollHorizontalImpulse
					+ FVector::UpVector * DeathRagdollUpwardImpulse;

			// A trace can report a graphical bone that has no PhysicsAsset body.
			// Sending that name to AddImpulseAtLocation silently drops the impulse,
			// which made non-ranged enemy meshes appear to fall straight down.
			FName ImpulseBone = ImpactData.HitBoneName;
			if (ImpulseBone.IsNone() || !MeshComponent->GetBodyInstance(ImpulseBone))
			{
				ImpulseBone = DeathRagdollFallbackImpulseBone;
			}
			if (!ImpulseBone.IsNone() && !MeshComponent->GetBodyInstance(ImpulseBone))
			{
				ImpulseBone = NAME_None;
			}

			if (ImpactData.bHasImpactPoint && MeshComponent->GetBodyInstance(ImpulseBone))
			{
				MeshComponent->AddImpulseAtLocation(
					Impulse, FVector(ImpactData.ImpactPoint), ImpulseBone);
			}
			else
			{
				MeshComponent->AddImpulse(Impulse, ImpulseBone, false);
			}
		}
	}
}

void ABaseCharacter::ResetLocalDeathRagdoll()
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (MeshComponent)
	{
		MeshComponent->SetPhysicsLinearVelocity(FVector::ZeroVector);
		MeshComponent->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		MeshComponent->SetSimulatePhysics(false);
		MeshComponent->SetAllBodiesSimulatePhysics(false);
		MeshComponent->SetPhysicsBlendWeight(0.0f);
		if (!InitialMeshCollisionProfileName.IsNone())
		{
			MeshComponent->SetCollisionProfileName(InitialMeshCollisionProfileName);
		}
		MeshComponent->SetCollisionEnabled(InitialMeshCollisionEnabled);
		MeshComponent->SetRelativeTransform(
			InitialMeshRelativeTransform,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
	}

	bLocalDeathRagdollApplied = false;
}
