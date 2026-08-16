#include "SWShipWakeEmitterComponent.h"

#include "GameFramework/Actor.h"
#include "SWShipWakeSubsystem.h"

USWShipWakeEmitterComponent::USWShipWakeEmitterComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(true);
}

void USWShipWakeEmitterComponent::BeginPlay()
{
	Super::BeginPlay();
	FVector2D Apex;
	FVector2D Forward;
	ResolveKelvinFrame(Apex, Forward);
	LastEmissionPosition = Apex;
	TrajectoryAnchors = { Apex };
	bHasEmissionOrigin = true;
}

void USWShipWakeEmitterComponent::ResolveKelvinFrame(FVector2D& OutApex, FVector2D& OutForward) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		OutApex = FVector2D::ZeroVector;
		OutForward = FVector2D(1.0f, 0.0f);
		return;
	}

	const FVector WorldApex = Owner->GetActorTransform().TransformPosition(KelvinApexLocalOffset);
	const float YawRadians = FMath::DegreesToRadians(KelvinDirectionYawDegrees);
	const FVector LocalDirection(FMath::Cos(YawRadians), FMath::Sin(YawRadians), 0.0f);
	const FVector WorldDirection = Owner->GetActorTransform().TransformVectorNoScale(LocalDirection);
	OutApex = FVector2D(WorldApex);
	OutForward = FVector2D(WorldDirection).GetSafeNormal();
}

void USWShipWakeEmitterComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World || !Owner->HasAuthority())
	{
		return;
	}

	const FVector Velocity = Owner->GetVelocity();
	const float HorizontalSpeed = FVector2D(Velocity.X, Velocity.Y).Size();
	SmoothedSpectrumSpeed = SmoothedSpectrumSpeed <= UE_SMALL_NUMBER
		? HorizontalSpeed
		: FMath::FInterpTo(
			SmoothedSpectrumSpeed,
			HorizontalSpeed,
			DeltaTime,
			FMath::Max(SpectrumSpeedSmoothingRate, 0.1f));
	if (HorizontalSpeed < MinimumSpeedCmPerSecond)
	{
		return;
	}

	FVector2D Apex;
	FVector2D Forward;
	ResolveKelvinFrame(Apex, Forward);
	if (Forward.IsNearlyZero())
	{
		return;
	}

	USWShipWakeSubsystem* Subsystem = World->GetSubsystem<USWShipWakeSubsystem>();
	const double ServerTime = Subsystem ? Subsystem->GetServerTime() : World->GetTimeSeconds();
	const bool bMovedFarEnough = !bHasEmissionOrigin
		|| FVector2D::DistSquared(Apex, LastEmissionPosition) >= FMath::Square(EmissionDistanceCm);
	const bool bWaitedLongEnough = ServerTime - LastEmissionServerTime >= MinimumEmissionInterval;
	if (!bMovedFarEnough || !bWaitedLongEnough)
	{
		return;
	}

	PublishWakeState(Apex, Forward, HorizontalSpeed);
	LastEmissionPosition = Apex;
	LastEmissionServerTime = ServerTime;
	bHasEmissionOrigin = true;
}

void USWShipWakeEmitterComponent::PublishWakeState(
	const FVector2D& Apex,
	const FVector2D& Forward,
	const float HorizontalSpeed)
{
	USWShipWakeSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<USWShipWakeSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	const float SafeHullLength = FMath::Max(HullLengthCm, 100.0f);
	const float SafePressureSize = FMath::Max(PressureSizeCm, 10.0f);
	const float Froude = HorizontalSpeed / FMath::Sqrt(980.0f * SafePressureSize);
	const float SpeedAlpha = FMath::SmoothStep(
		MinimumSpeedCmPerSecond,
		MinimumSpeedCmPerSecond * 3.0f,
		HorizontalSpeed);

	FSWShipWakeEvent Event;
	const uint32 OwnerHash = static_cast<uint32>(GetTypeHash(GetOwner()->GetPathName())) & 0x7FFFFFFFu;
	Event.EventId = static_cast<int32>(FMath::Max(OwnerHash, 1u));
	Event.Origin = Apex;
	Event.Forward = Forward;
	const float PathSpacing = FMath::Max(TrajectorySampleDistanceCm, 50.0f);
	if (TrajectoryAnchors.IsEmpty())
	{
		TrajectoryAnchors.Add(Apex);
	}
	else if (FVector2D::Distance(Apex, TrajectoryAnchors[0]) >= PathSpacing)
	{
		TrajectoryAnchors.Insert(Apex, 0);
		TrajectoryAnchors.SetNum(FMath::Min(TrajectoryAnchors.Num(), 15), EAllowShrinking::No);
	}
	Event.TrajectoryPoints.Add(Apex);
	for (const FVector2D& Anchor : TrajectoryAnchors)
	{
		if (!Anchor.Equals(Event.TrajectoryPoints.Last(), 1.0f) && Event.TrajectoryPoints.Num() < 16)
		{
			Event.TrajectoryPoints.Add(Anchor);
		}
	}
	Event.UpdateServerTime = Subsystem->GetServerTime();
	Event.Amplitude = MaximumAmplitudeCm * SpeedAlpha * FMath::Clamp(Froude / 0.35f, 0.40f, 1.50f);
	Event.SpeedCmPerSecond = FMath::Max(SmoothedSpectrumSpeed, MinimumSpeedCmPerSecond);
	Event.AdvectionSpeedCmPerSecond = HorizontalSpeed;
	Event.PressureSizeCm = SafePressureSize;
	Event.LongitudinalScale = FMath::Max(LongitudinalScale, 0.1f);
	Event.LateralScale = FMath::Max(LateralScale, 0.1f);
	Event.NearHullSuppressDistanceCm = FMath::Max(NearHullSuppressDistanceCm, 0.0f);
	Event.HullLengthCm = SafeHullLength;
	Event.SternOffsetCm = FMath::Clamp(SternOffsetCm, 0.0f, SafeHullLength);
	Event.BeamWidthCm = FMath::Max(BeamWidthCm, 50.0f);
	Event.DraftCm = FMath::Max(DraftCm, 1.0f);
	Event.WakeLengthCm = FMath::Clamp(
		SafeHullLength * WakeLengthMultiplier * FMath::Clamp(Froude / 0.35f, 0.60f, 2.0f),
		SafeHullLength * 2.0f,
		60000.0f);
	Event.StateLifetime = FMath::Max(LifetimeSeconds, 0.1f);
	Event.TransverseStrength = FMath::Max(TransverseStrength, 0.0f);
	Event.DivergentStrength = FMath::Max(DivergentStrength, 0.0f);
	Event.SternStrength = FMath::Max(SternStrength, 0.0f);
	Event.SternPhaseOffsetRadians = FMath::Fmod(FMath::Max(SternPhaseOffsetRadians, 0.0f), 2.0f * PI);
	MulticastUpdateWakeEvent(Event);
}

void USWShipWakeEmitterComponent::MulticastUpdateWakeEvent_Implementation(const FSWShipWakeEvent& Event)
{
	if (UWorld* World = GetWorld())
	{
		if (USWShipWakeSubsystem* Subsystem = World->GetSubsystem<USWShipWakeSubsystem>())
		{
			Subsystem->AddOrUpdateEvent(Event);
		}
	}
}

