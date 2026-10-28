#include "Components/SWFoleyComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "Audio/SWFoleyAudioProfile.h"
#include "Audio/SWWorldAudio.h"
#include "BasePlayer.h"
#include "Components/BaseHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "SwimmingComponent.h"

bool USWFoleyComponent::AdmitSurfaceStroke(USkeletalMeshComponent* Mesh, ESWFoleySide Side, double Now)
{
	const auto* Character = Cast<ACharacter>(GetOwner());
	const auto* World = GetWorld();
	if (!bUseAnimationDrivenSurfaceStrokes || !bEnabled || !IsActive() || !Character || !World ||
		!World->IsGameWorld() || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer ||
		Character->IsHidden() || Character->IsActorBeingDestroyed() || Mesh != Character->GetMesh() ||
		!Mesh || Mesh->IsSimulatingPhysics() || (Side != ESWFoleySide::Left && Side != ESWFoleySide::Right)) { return false; }
	const auto* Swimming = Character->FindComponentByClass<USwimmingComponent>();
	if (!Swimming || !Swimming->IsCustomSwimming() || Swimming->IsUnderwater()) { return false; }
	if (const auto* Health = Character->FindComponentByClass<UBaseHealthComponent>(); Health && Health->IsDead()) { return false; }
	if (const auto* Player = Cast<ABasePlayer>(Character); Player && Player->IsMountedForDamage()) { return false; }
	const int32 Index = Side == ESWFoleySide::Left ? 0 : 1;
	// Blended poses, linked instances and mirrored clips cannot double a catch.
	// This admits 2 catches/second at the current 1s freestyle cycle without a distance clock.
	if (!FMath::IsFinite(Now) || Now - LastSurfaceStrokeTime < 0.16 ||
		Now - LastSurfaceStrokeBySide[Index] < 0.32 || Now - LastFullWaterEntryTime < 0.2) { return false; }
	LastSurfaceStrokeTime = Now;
	LastSurfaceStrokeBySide[Index] = Now;
	return true;
}

bool USWFoleyComponent::HandleSurfaceSwimStroke(USkeletalMeshComponent* Mesh, ESWFoleySide Side, UAnimSequenceBase* Sequence)
{
	UWorld* World = GetWorld();
	if (!World || !AdmitSurfaceStroke(Mesh, Side, World->GetTimeSeconds())) { return false; }
	FVector Location = GetOwner()->GetActorLocation();
	if (const auto* Profile = ProfileOverride.Get())
	{
		FSWFoleySoundSet Hand; Hand.Contact = ESWFoleyContact::Hand;
		Location = ResolveContactLocation(*Profile, Side, &Hand);
	}
	const bool bPlayed = USWWorldAudioSubsystem::PlayEvent(this, ESWWorldSound::SwimStroke, Location, GetOwner(), 0.5f);
	static const auto* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("SW.Audio.DebugEvents"));
	if (Debug && Debug->GetInt())
	{
		UE_LOG(LogTemp, Display, TEXT("SW_SWIM_CATCH World=%s NetMode=%d Actor=%s Side=%s Clip=%s Time=%.3f Played=%d"),
			*World->GetName(), int32(World->GetNetMode()), *GetOwner()->GetName(),
			Side == ESWFoleySide::Left ? TEXT("L") : TEXT("R"), *GetNameSafe(Sequence), World->GetTimeSeconds(), bPlayed);
	}
	return bPlayed;
}
