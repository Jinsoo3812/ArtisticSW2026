#include "Recovery/StuckRecoveryCollision.h"

#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	FCollisionShape Shape(const ACharacter& Character, float Inset = 0.f)
	{
		const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
		const float Radius = FMath::Max(1.f, Capsule->GetScaledCapsuleRadius() - Inset);
		return FCollisionShape::MakeCapsule(Radius,
			FMath::Max(Radius, Capsule->GetScaledCapsuleHalfHeight() - Inset));
	}

	FCollisionQueryParams Params(const ACharacter& Character)
	{
		FCollisionQueryParams Result(SCENE_QUERY_STAT(StuckRecovery), false, &Character);
		Result.bFindInitialOverlaps = true;
		// Match the capsule's existing movement exclusions (mounted equipment, etc.).
		Result.AddIgnoredActors(Character.GetCapsuleComponent()->GetMoveIgnoreActors());
		Result.AddIgnoredComponents(Character.GetCapsuleComponent()->GetMoveIgnoreComponents());
		return Result;
	}

	bool Overlaps(const ACharacter& Character, const FVector& Location, float Inset)
	{
		const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
		return Character.GetWorld()->OverlapBlockingTestByChannel(Location, Capsule->GetComponentQuat(),
			Capsule->GetCollisionObjectType(), Shape(Character, Inset), Params(Character),
			FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
	}

	bool Sweep(const ACharacter& Character, const FVector& Start, const FVector& End,
		FHitResult& Hit, float Inset = 0.f)
	{
		const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
		return Character.GetWorld()->SweepSingleByChannel(Hit, Start, End, Capsule->GetComponentQuat(),
			Capsule->GetCollisionObjectType(), Shape(Character, Inset), Params(Character),
			FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
	}

	bool HasLanding(const ACharacter& Character, const FVector& Candidate, float MaxDrop)
	{
		FHitResult Floor;
		// A capsule floor sweep verifies support for the actual character footprint.
		return Sweep(Character, Candidate, Candidate - FVector(0, 0, FMath::Max(1.f, MaxDrop)), Floor)
			&& !Floor.bStartPenetrating && Character.GetCharacterMovement()->IsWalkable(Floor);
	}

	bool HasPath(const ACharacter& Character, const FVector& Candidate,
		const FStuckEscapeSettings& Settings)
	{
		const FVector Origin = Character.GetActorLocation();
		FHitResult Hit;
		if (!Sweep(Character, Origin, Candidate, Hit)) return true;
		// Permit a clear upward-then-forward path without crossing a blocking wall.
		if (!Hit.bStartPenetrating)
		{
			const FVector RaisedOrigin(Origin.X, Origin.Y, Candidate.Z);
			return !Sweep(Character, Origin, RaisedOrigin, Hit)
				&& !Sweep(Character, RaisedOrigin, Candidate, Hit);
		}
		if (!Settings.bAllowPenetrationRecovery) return false;

		TArray<FOverlapResult> InitialOverlaps;
		const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
		Character.GetWorld()->OverlapMultiByChannel(InitialOverlaps, Origin, Capsule->GetComponentQuat(),
			Capsule->GetCollisionObjectType(), Shape(Character), Params(Character),
			FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
		// Reverse sweep: only the final, bounded segment into an original blocker may fail.
		// An intervening wall or any new blocker rejects the candidate.
		if (!Sweep(Character, Candidate, Origin, Hit) || Hit.bStartPenetrating) return false;
		if (FVector::Distance(Hit.Location, Origin) > FMath::Max(0.f, Settings.MaxPenetrationExitDistance)) return false;
		const bool bOriginalBlocker = InitialOverlaps.ContainsByPredicate([&Hit](const FOverlapResult& Overlap)
		{
			return Overlap.bBlockingHit && Overlap.Component == Hit.Component;
		});
		if (!bOriginalBlocker) return false;
		// A sweep stops at the first blocker. A second query excluding the original
		// overlaps catches a new obstacle hidden behind that first contact.
		FCollisionQueryParams ExitParams = Params(Character);
		for (const FOverlapResult& Overlap : InitialOverlaps)
			if (Overlap.bBlockingHit) ExitParams.AddIgnoredComponent(Overlap.Component.Get());
		return !Character.GetWorld()->SweepSingleByChannel(Hit, Candidate, Origin, Capsule->GetComponentQuat(),
			Capsule->GetCollisionObjectType(), Shape(Character), ExitParams,
			FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
	}
}

bool StuckRecoveryCollision::IsPenetrating(const ACharacter& Character, float Inset)
{
	return Overlaps(Character, Character.GetActorLocation(), FMath::Max(0.1f, Inset));
}

bool StuckRecoveryCollision::IsEnclosed(const ACharacter& Character, const FStuckDetectionSettings& Settings)
{
	// One wall is never sufficient evidence. All eight horizontal exits must be blocked.
	const FVector Origin = Character.GetActorLocation();
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const float Angle = Index * UE_PI / 4.f;
		const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
		FHitResult Hit;
		if (!Sweep(Character, Origin, Origin + Direction * FMath::Max(1.f, Settings.ObstructionProbeDistance),
			Hit, Settings.PenetrationInset)) return false;
	}
	return true;
}

bool StuckRecoveryCollision::FindDestination(const ACharacter& Character,
	const FStuckEscapeSettings& Settings, FVector& OutDestination)
{
	const FVector Origin = Character.GetActorLocation();
	const FRotator YawRotation(0.f, Character.GetActorRotation().Yaw, 0.f);
	const FVector Forward = YawRotation.Vector();
	const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	const float ForwardDistance = FMath::Max(0.f, Settings.ForwardDistance);
	const float UpDistance = FMath::Max(0.f, Settings.UpDistance);
	const float SideDistance = Character.GetCapsuleComponent()->GetScaledCapsuleRadius() * 2.f;
	const FVector Candidates[] = {
		Origin + Forward * ForwardDistance + FVector(0, 0, UpDistance),
		Origin + Forward * ForwardDistance + FVector(0, 0, UpDistance + SideDistance),
		Origin + Forward * (ForwardDistance * 0.5f) + FVector(0, 0, UpDistance),
		Origin + FVector(0, 0, UpDistance),
		Origin + Forward * ForwardDistance + Right * SideDistance + FVector(0, 0, UpDistance),
		Origin + Forward * ForwardDistance - Right * SideDistance + FVector(0, 0, UpDistance),
		Origin + Forward * (ForwardDistance * 0.5f) + Right * SideDistance + FVector(0, 0, UpDistance + SideDistance),
		Origin + Forward * (ForwardDistance * 0.5f) - Right * SideDistance + FVector(0, 0, UpDistance + SideDistance)
	};
	for (int32 Index = 0; Index < FMath::Clamp(Settings.MaxEscapeAttempts, 1, 8); ++Index)
	{
		const FVector& Candidate = Candidates[Index];
		if (Candidate.Equals(Origin, 1.f) || Overlaps(Character, Candidate, 0.f)) continue;
		if (!HasLanding(Character, Candidate, Settings.MaxLandingDrop) || !HasPath(Character, Candidate, Settings)) continue;
		OutDestination = Candidate;
		return true;
	}
	return false;
}
