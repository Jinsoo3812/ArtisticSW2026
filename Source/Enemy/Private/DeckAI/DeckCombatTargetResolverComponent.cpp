#include "DeckAI/DeckCombatTargetResolverComponent.h"

#include "BaseEnemy.h"
#include "Components/BaseHealthComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckWaypointMovementInterface.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ShipAI/EnemyShip.h"

DEFINE_LOG_CATEGORY_STATIC(LogDeckTargetTracking, Log, All);

UDeckCombatTargetResolverComponent::UDeckCombatTargetResolverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}
void UDeckCombatTargetResolverComponent::Reset()
{
	TrackedTarget.Reset(); TrackedShip.Reset(); CachedAnchor = LastEvidence = FDeckTargetAnchor();
	CachedRevision = INDEX_NONE; LastSampleTime = LastEvidenceTime = UnresolvedSince = -1.;
}
bool UDeckCombatTargetResolverComponent::HasExpiredEvidence(AActor* Target) const
{
	return TrackedTarget == Target && !CachedAnchor.IsValid() && UnresolvedSince >= 0. && GetWorld()
		&& GetWorld()->GetTimeSeconds() - UnresolvedSince >= FMath::Max(0.3f, MissingEvidenceGrace);
}
bool UDeckCombatTargetResolverComponent::ResolveFor(const AActor* Observer, AActor* Target, FDeckTargetAnchor& Out)
{
	Out = FDeckTargetAnchor();
	UDeckCombatTargetResolverComponent* Resolver = Observer ? Observer->FindComponentByClass<UDeckCombatTargetResolverComponent>() : nullptr;
	return Resolver && Resolver->Resolve(Target, Out);
}
bool UDeckCombatTargetResolverComponent::Resolve(AActor* Target, FDeckTargetAnchor& Out)
{
	Out = FDeckTargetAnchor();
	const IDeckWaypointMovementInterface* Mover = Cast<IDeckWaypointMovementInterface>(GetOwner());
	AEnemyShip* Ship = Mover ? Mover->GetDeckHostShip() : nullptr;
	const UDeckWalkAreaComponent* Area = IsValid(Ship) ? Ship->GetDeckWalkAreaComponent() : nullptr;
	const ABaseEnemy* Enemy = Cast<ABaseEnemy>(GetOwner());
	if (!GetOwner()->HasAuthority() || !IsValid(Target) || Target->IsActorBeingDestroyed() || !Area || !Area->IsReady()
		|| !Enemy || !Enemy->CanEngageActor(Target) || (Enemy->GetHealthComponent() && Enemy->GetHealthComponent()->IsDead()))
	{
		Reset(); Out.FailureReason = TEXT("InvalidTargetOrHost"); return false;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (TrackedTarget != Target || TrackedShip != Ship || CachedRevision != Area->GetRevision())
	{
		// Rebuild never revives an old node handle or extends the evidence lifetime.
		if (TrackedTarget != Target || TrackedShip != Ship) Reset();
		TrackedTarget = Target; TrackedShip = Ship; CachedRevision = Area->GetRevision(); LastSampleTime = -1.;
	}
	if (LastSampleTime >= 0. && Now - LastSampleTime < 0.05)
	{
		Out = CachedAnchor; Out.ActualWorldLocation = Target->GetActorLocation(); return Out.IsValid();
	}
	const EDeckTargetAnchorSource PreviousSource = CachedAnchor.Source;
	const FName PreviousReason = CachedAnchor.FailureReason;
	FDeckTargetAnchor Next;
	const ACharacter* Character = Cast<ACharacter>(Target);
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	const bool bGrounded = Movement && Movement->IsMovingOnGround() && Movement->CurrentFloor.IsWalkableFloor();
	const UPrimitiveComponent* Support = bGrounded ? Movement->CurrentFloor.HitResult.GetComponent() : nullptr;
	const bool bForeignSupport = bGrounded && !Area->IsTrackingSupport(Support);
	const FVector Feet = Area->ToLocal(Area->GetActorFeetWorld(*Target));
	FDeckWalkLocation Floor;
	if (!bForeignSupport && Area->ResolveActorOnDeck(*Target, Floor))
	{
		Next.LocalCenter = FVector(Feet.X, Feet.Y, Floor.LocalFloor.Z);
		Next.SurfaceId = Floor.SurfaceId;
		Next.Source = bGrounded && FMath::Abs(Feet.Z - Floor.LocalFloor.Z) > 10.f
			? EDeckTargetAnchorSource::SupportedObstacle : EDeckTargetAnchorSource::DeckFloor;
	}
	else if (!bForeignSupport)
	{
		// Grounded obstacles are fresh evidence, independent of the last deck landing or grace timer.
		const FVector Probe = bGrounded ? Area->ToLocal(Movement->CurrentFloor.HitResult.ImpactPoint) : Feet;
		FVector Center; FName Surface; FName Reason;
		const FName Preferred = !bGrounded && LastEvidence.IsValid() && Now - LastEvidenceTime <= MissingEvidenceGrace
			? LastEvidence.SurfaceId : NAME_None;
		if (Area->ResolveTrackingProjection(Probe, Preferred, MaximumProjectionDistance, MaximumProjectionHeight, Center, Surface, Reason))
		{
			Next.LocalCenter = FVector(Feet.X, Feet.Y, Center.Z); Next.SurfaceId = Surface;
			Next.Source = bGrounded ? EDeckTargetAnchorSource::SupportedObstacle : EDeckTargetAnchorSource::AirborneProjection;
		}
		else Next.FailureReason = Reason;
	}
	else Next.FailureReason = TEXT("ForeignSupport");
	if (Next.HasCurrentEvidence()) { LastEvidence = Next; LastEvidenceTime = Now; }
	else if (!bForeignSupport && LastEvidence.IsValid() && Now - LastEvidenceTime <= MissingEvidenceGrace
		&& FVector::Dist2D(Feet, LastEvidence.LocalCenter) <= MaximumProjectionDistance
		&& FMath::Abs(Feet.Z - LastEvidence.LocalCenter.Z) <= MaximumProjectionHeight
		&& Area->GetSurfaceNodeCount(LastEvidence.SurfaceId) > 0)
	{
		Next.LocalCenter = LastEvidence.LocalCenter; Next.SurfaceId = LastEvidence.SurfaceId;
		Next.Source = EDeckTargetAnchorSource::RecentAnchor;
	}
	if (Next.IsValid()) UnresolvedSince = -1.;
	else if (UnresolvedSince < 0.) UnresolvedSince = Now;
	Next.ActualWorldLocation = Target->GetActorLocation(); CachedAnchor = Next; LastSampleTime = Now; Out = Next;
	if (PreviousSource != Next.Source || PreviousReason != Next.FailureReason)
		UE_LOG(LogDeckTargetTracking, Verbose, TEXT("Observer=%s Ship=%s Target=%s Source=%d Surface=%s Reason=%s"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Ship), *GetNameSafe(Target), int32(Next.Source), *Next.SurfaceId.ToString(), *Next.FailureReason.ToString());
	if (bDrawTrackingDebug && Next.IsValid())
	{
		const FVector Center = Area->ToWorld(Next.LocalCenter);
		DrawDebugSphere(GetWorld(), Center, 15.f, 8, FColor::Cyan, false, 0.1f);
		DrawDebugLine(GetWorld(), Target->GetActorLocation(), Center, FColor::Yellow, false, 0.1f);
	}
	return Next.IsValid();
}
