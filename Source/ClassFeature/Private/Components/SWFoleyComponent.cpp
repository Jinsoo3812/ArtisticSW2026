#include "Components/SWFoleyComponent.h"

#include "Audio/SWFoleyAudioProfile.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/BaseHealthComponent.h"
#include "BasePlayer.h"
#include "SwimmingComponent.h"
#include "System/SWFoleySubsystem.h"

USWFoleyComponent::USWFoleyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
	SetIsReplicatedByDefault(false);
	for (double& Time : LastAdmission) { Time = -DBL_MAX; }
}

TSoftObjectPtr<USWFoleyAudioProfile> USWFoleyComponent::GetEffectiveProfile() const
{
	return ProfileOverride;
}

bool USWFoleyComponent::CanPresent(ESWFoleyGroup Group) const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UWorld* World = GetWorld();
	if (!bEnabled || !IsActive() || !Character || !World || !World->IsGameWorld() ||
		World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer || Character->IsActorBeingDestroyed()) { return false; }
	if (const auto* Health = Character->FindComponentByClass<UBaseHealthComponent>(); Health && Health->IsDead()) { return false; }
	if (Character->GetMesh() && Character->GetMesh()->IsSimulatingPhysics()) { return false; }
	if (const auto* Player = Cast<ABasePlayer>(Character); Player && Player->IsMountedForDamage()) { return false; }
	if (const auto* Swimming = Character->FindComponentByClass<USwimmingComponent>(); Swimming && Swimming->IsCustomSwimming()) { return false; }
	if (const auto* Movement = Character->GetCharacterMovement(); Movement && Movement->IsSwimming()) { return false; }
	if (Group == ESWFoleyGroup::Footstep || Group == ESWFoleyGroup::Scuff)
	{
		const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		if (!Movement || !Movement->IsMovingOnGround()) { return false; }
	}
	return true;
}

bool USWFoleyComponent::PlayFoleyEvent(const FSWFoleyEvent& Event)
{
	if (!CanPresent(ESWFoleyGroup::Other)) { return false; }
	if (auto* Service = GetWorld()->GetSubsystem<USWFoleySubsystem>()) { return Service->Submit(this, Event); }
	return false;
}

bool USWFoleyComponent::PrepareLocalAudio()
{
	if (!CanPresent(ESWFoleyGroup::Other)) { return false; }
	if (auto* Service = GetWorld()->GetSubsystem<USWFoleySubsystem>()) { return Service->PrepareLocalAudio(this); }
	return false;
}

void USWFoleyComponent::HandleAnimNotify(USkeletalMeshComponent* Mesh, const FSWFoleyEvent& Event)
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	// Retarget followers and modular clothes cannot create a second contact stream.
	if (!Character || Mesh != Character->GetMesh()) { return; }
	if (bUseMovementEventsForJumpAndLand && (Event.Event == SW::Foley::Jump || Event.Event == SW::Foley::Land)) { return; }
	PlayFoleyEvent(Event);
}

void USWFoleyComponent::PlayJump(float HorizontalSpeed, float AgeSeconds)
{
	if (!bUseMovementEventsForJumpAndLand || !FMath::IsFinite(HorizontalSpeed)) { return; }
	FSWFoleyEvent Event;
	Event.Event = SW::Foley::Jump;
	Event.VolumeMultiplier = FMath::GetMappedRangeValueClamped(FVector2D(0.f, 500.f), FVector2D(0.5f, 1.f), HorizontalSpeed);
	Event.AgeSeconds = AgeSeconds;
	PlayFoleyEvent(Event);
}

void USWFoleyComponent::PlayLanding(float ImpactSpeed, float AgeSeconds)
{
	if (!bUseMovementEventsForJumpAndLand || !FMath::IsFinite(ImpactSpeed)) { return; }
	FSWFoleyEvent Event;
	Event.Event = SW::Foley::Land;
	Event.VolumeMultiplier = FMath::GetMappedRangeValueClamped(FVector2D(500.f, 900.f), FVector2D(0.5f, 1.5f), FMath::Abs(ImpactSpeed));
	Event.AgeSeconds = AgeSeconds;
	PlayFoleyEvent(Event);
}

FVector USWFoleyComponent::ResolveContactLocation(const USWFoleyAudioProfile& Profile, ESWFoleySide Side,
	const FSWFoleySoundSet* Definition) const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character) { return FVector::ZeroVector; }
	const ESWFoleyContact Contact = Definition ? Definition->Contact : ESWFoleyContact::Foot;
	const bool bHand = Contact == ESWFoleyContact::Hand;
	const bool bSocket = Contact == ESWFoleyContact::Socket;
	if (bSocket || ((Contact == ESWFoleyContact::Foot || bHand) && Side != ESWFoleySide::None))
	{
		const bool bLeft = Side == ESWFoleySide::Left;
		const FName SourceBone = bSocket ? Definition->ContactSocket :
			(bHand ? (bLeft ? Profile.SourceLeftHand : Profile.SourceRightHand) : (bLeft ? Profile.SourceLeftFoot : Profile.SourceRightFoot));
		const FName VisualBone = bSocket ? SourceBone :
			(bHand ? (bLeft ? Profile.VisualLeftHand : Profile.VisualRightHand) : (bLeft ? Profile.VisualLeftFoot : Profile.VisualRightFoot));
		USkeletalMeshComponent* Visual = nullptr;
		// Use an attached visible retarget follower if the character hides its source mesh.
		if (const auto* SourceMesh = Character->GetMesh())
		{
			for (USceneComponent* Child : SourceMesh->GetAttachChildren())
			{
				if (auto* Candidate = Cast<USkeletalMeshComponent>(Child); Candidate && Candidate->IsVisible() &&
					Candidate->DoesSocketExist(VisualBone.IsNone() ? SourceBone : VisualBone)) { Visual = Candidate; break; }
			}
		}
		const FName EffectiveVisualBone = VisualBone.IsNone() ? SourceBone : VisualBone;
		if (Visual && Visual->IsRegistered() && Visual->WasRecentlyRendered(0.2f) && Visual->DoesSocketExist(EffectiveVisualBone))
		{
			return Visual->GetSocketLocation(EffectiveVisualBone);
		}
		const USkeletalMeshComponent* Source = Character->GetMesh();
		if (Source && Source->IsRegistered() && Source->WasRecentlyRendered(0.2f) && Source->DoesSocketExist(SourceBone))
		{
			return Source->GetSocketLocation(SourceBone);
		}
	}
	// Offscreen/URO poses can be stale. Use a current movement-space contact without waking all bones.
	return Character->GetActorLocation() - Character->GetActorUpVector() * Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
}

void USWFoleyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (auto* Service = GetWorld() ? GetWorld()->GetSubsystem<USWFoleySubsystem>() : nullptr) { Service->Cancel(this); }
	for (double& Time : LastAdmission) { Time = -DBL_MAX; }
	Super::EndPlay(EndPlayReason);
}
