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
#include "Audio/SWWorldAudio.h"
#include "TimerManager.h"
#include "WaterSurfaceQueryLibrary.h"

USWFoleyComponent::USWFoleyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
	SetIsReplicatedByDefault(false);
	for (double& Time : LastAdmission) { Time = -DBL_MAX; }
}

void USWFoleyComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetWorld()->IsGameWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer)
	{
		GetWorld()->GetTimerManager().SetTimer(MovementAudioTimer, this, &ThisClass::UpdateMovementAudio, 0.1f, true);
	}
}

void USWFoleyComponent::UpdateMovementAudio()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !GetWorld() || GetWorld()->bIsTearingDown) { return; }
	const auto* Movement = Character->GetCharacterMovement();
	const auto* Swimming = Character->FindComponentByClass<USwimmingComponent>();
	const auto* Health = Character->FindComponentByClass<UBaseHealthComponent>();
	const auto* Player = Cast<ABasePlayer>(Character);
	UPrimitiveComponent* Base = Character->GetMovementBase();
	// Distances are centimetres even on scaled landscape collision or ship meshes.
	// Remove base translation/rotation, but never divide travel by its authored scale.
	const FVector Position = Base ? Base->GetComponentTransform().InverseTransformPositionNoScale(Character->GetActorLocation()) : Character->GetActorLocation();
	const bool bGrounded = Movement && Movement->IsMovingOnGround();
	int32 WaterState = Swimming ? (Swimming->IsCustomSwimming() ? (Swimming->IsUnderwater() ? 3 : 2) : (Swimming->IsInShallowWater() ? 1 : 0)) : 0;
	const bool bAllowed = bEnabled && IsActive() && !Character->IsHidden() && (!Health || !Health->IsDead()) &&
		(!Player || !Player->IsMountedForDamage()) && Character->GetMesh() && !Character->GetMesh()->IsSimulatingPhysics();
	if (!Swimming && bDetectWaterWithoutSwimming && bAllowed)
	{
		if (bGrounded) { SampledNPCWaterState = 0; }
		else if (Movement && Movement->IsFalling() && GetWorld()->GetTimeSeconds() >= NextWaterProbeTime)
		{
			NextWaterProbeTime = GetWorld()->GetTimeSeconds() + 0.2;
			auto* Foley = GetWorld()->GetSubsystem<USWFoleySubsystem>();
			if (Foley && Foley->AdmitWaterProbe(Character->GetActorLocation()))
			{
				float SurfaceZ = 0.f;
				const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				const float Depth = FWaterSurfaceQueryLibrary::QueryWaterSurface(GetWorld(), Character->GetActorLocation(), SurfaceZ, true)
					? SurfaceZ - Character->GetActorLocation().Z : -BIG_NUMBER;
				SampledNPCWaterState = Depth > -HalfHeight * 0.25f ? 2 : (Depth > -HalfHeight + 10.f ? 1 : 0);
			}
		}
		WaterState = SampledNPCWaterState;
	}
	const bool bContinuous = bMovementInitialized && LastMovementBase.Get() == Base;
	const float Travel = bContinuous ? FVector::Dist2D(Position, LastMovementPosition) : 0.f;
	const bool bVerticalSwim = Swimming && (Swimming->HasVerticalSwimInput() ||
		(WaterState == 3 && Swimming->GetDepthMode() == ESwimDepthMode::Submerged));
	const float SwimDistance = bContinuous ? (bVerticalSwim ? FVector::Distance(Position, LastMovementPosition) : FVector::Dist2D(Position, LastMovementPosition)) : 0.f;
	const bool bTeleported = Travel > 400.f;
	if (bAllowed && bMovementInitialized && !bTeleported)
	{
		const double Now = GetWorld()->GetTimeSeconds();
		// A floating swimmer can briefly cross the shallow threshold as waves
		// pass. That is not another body falling into the sea. Re-arm only after
		// actually leaving the water or standing in the shallows.
		if (WaterState == 0)
		{
			if (OutOfWaterSince < 0.) { OutOfWaterSince = Now; }
			if (Now - OutOfWaterSince >= 0.5) { bFullWaterEntryArmed = true; }
		}
		else
		{
			OutOfWaterSince = -1.;
			if (WaterState == 1 && bGrounded) { bFullWaterEntryArmed = true; }
		}
		if (WaterState != LastWaterState)
		{
			// A first ankle-deep wade must not consume the full-body entry sound's
			// cooldown when a fall crosses shallow water and swimming in two samples.
			if (WaterState >= 2 && LastWaterState < 2 && bFullWaterEntryArmed && Now - LastFullWaterEntryTime > 1.0)
			{
				USWWorldAudioSubsystem::PlayEvent(this, ESWWorldSound::WaterEnter, Character->GetActorLocation(), Character, 0.8f);
				LastFullWaterEntryTime = Now;
				bFullWaterEntryArmed = false;
				LastWaterSoundTime = Now;
			}
			else if (LastWaterState == 0 && WaterState == 1 && Now - LastWaterSoundTime > 0.6)
			{
				USWWorldAudioSubsystem::PlayEvent(this, ESWWorldSound::WaterWadeEnter, Character->GetActorLocation(), Character, 0.55f);
				LastWaterSoundTime = Now;
			}
			else if (LastWaterState > 0 && WaterState == 0 && Now - LastWaterSoundTime > 0.6)
			{
				USWWorldAudioSubsystem::PlayEvent(this, ESWWorldSound::WaterExit, Character->GetActorLocation(), Character, 0.6f);
				LastWaterSoundTime = Now;
			}
		}
		if (Swimming && WaterState >= 2 && (!bUseAnimationDrivenSurfaceStrokes || WaterState == 3) && SwimDistance > 1.f && SwimDistance < 400.f &&
			(Character->GetVelocity().SizeSquared2D() > FMath::Square(50.f) || (bVerticalSwim && FMath::Abs(Character->GetVelocity().Z) > 50.f)))
		{
			SwimTravel += SwimDistance;
			if (SwimTravel >= 140.f && Now - LastWaterSoundTime >= 0.65)
			{
				USWWorldAudioSubsystem::PlayEvent(this, WaterState == 3 ? ESWWorldSound::UnderwaterStroke : ESWWorldSound::SwimStroke,
					Character->GetActorLocation(), Character, 0.5f);
				SwimTravel = 0.f;
				LastWaterSoundTime = Now;
			}
		}
		else { SwimTravel = 0.f; }
		if (bUseDistanceDrivenSteps && bGrounded && bWasGrounded && CanPresent(ESWFoleyGroup::Footstep))
		{
			StepTravel += Travel;
			const bool bRunning = Travel / 0.1f > 280.f;
			if (Travel > 1.f && StepTravel >= FMath::Max(50.f, bRunning ? RunStepDistance : WalkStepDistance))
			{
				FSWFoleyEvent Event;
				Event.Event = bRunning ? SW::Foley::Run : SW::Foley::Walk;
				Event.Side = bNextLeft ? ESWFoleySide::Left : ESWFoleySide::Right;
				PlayFoleyEvent(Event);
				bNextLeft = !bNextLeft;
				StepTravel = 0.f;
			}
		}
		else { StepTravel = 0.f; }
		if ((bUseDistanceDrivenSteps || bPollMovementForJumpAndLand) && bGrounded && !bWasGrounded && LastVerticalSpeed < -150.f) { PlayLanding(LastVerticalSpeed); }
		if ((bUseDistanceDrivenSteps || bPollMovementForJumpAndLand) && !bGrounded && bWasGrounded && Character->GetVelocity().Z > 150.f) { PlayJump(Character->GetVelocity().Size2D()); }
	}
	else { StepTravel = 0.f; SwimTravel = 0.f; }
	LastMovementBase = Base;
	LastMovementPosition = Position;
	LastWaterState = WaterState;
	bWasGrounded = bGrounded;
	LastVerticalSpeed = Character->GetVelocity().Z;
	bMovementInitialized = bAllowed;
}

TSoftObjectPtr<USWFoleyAudioProfile> USWFoleyComponent::GetEffectiveProfile() const
{
	return ProfileOverride;
}

bool USWFoleyComponent::CanPresent(ESWFoleyGroup Group) const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UWorld* World = GetWorld();
	if (!bEnabled || !IsActive() || !Character || Character->IsHidden() || !World || !World->IsGameWorld() ||
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
	if (bUseDistanceDrivenSteps) { return; } // A character has one contact clock.
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
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(MovementAudioTimer); }
	if (auto* Service = GetWorld() ? GetWorld()->GetSubsystem<USWFoleySubsystem>() : nullptr) { Service->Cancel(this); }
	for (double& Time : LastAdmission) { Time = -DBL_MAX; }
	Super::EndPlay(EndPlayReason);
}
