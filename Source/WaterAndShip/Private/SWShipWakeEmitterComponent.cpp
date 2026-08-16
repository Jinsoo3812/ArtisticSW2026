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
	if (const AActor* Owner = GetOwner())
	{
		const FVector Location = Owner->GetActorLocation();
		LastEmissionPosition = FVector2D(Location.X, Location.Y);
		bHasEmissionOrigin = true;
	}
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

	FVector2D Forward(Owner->GetActorForwardVector());
	Forward.Normalize();
	if (Forward.IsNearlyZero())
	{
		return;
	}

	const FVector Location = Owner->GetActorLocation();
	const FVector2D Position(Location.X, Location.Y);
	USWShipWakeSubsystem* Subsystem = World->GetSubsystem<USWShipWakeSubsystem>();
	const double ServerTime = Subsystem ? Subsystem->GetServerTime() : World->GetTimeSeconds();
	const bool bMovedFarEnough = !bHasEmissionOrigin
		|| FVector2D::DistSquared(Position, LastEmissionPosition) >= FMath::Square(EmissionDistanceCm);
	const bool bWaitedLongEnough = ServerTime - LastEmissionServerTime >= MinimumEmissionInterval;
	if (!bMovedFarEnough || !bWaitedLongEnough)
	{
		return;
	}

	PublishWakeState(Location, Forward, HorizontalSpeed);
	LastEmissionPosition = Position;
	LastEmissionServerTime = ServerTime;
	bHasEmissionOrigin = true;
}

void USWShipWakeEmitterComponent::PublishWakeState(
	const FVector& OwnerLocation,
	const FVector2D& Forward,
	const float HorizontalSpeed)
{
	USWShipWakeSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<USWShipWakeSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	const float SafeHullLength = FMath::Max(HullLengthCm, 100.0f);
	const float Froude = HorizontalSpeed / FMath::Sqrt(980.0f * SafeHullLength);
	const float SpeedAlpha = FMath::SmoothStep(
		MinimumSpeedCmPerSecond,
		MinimumSpeedCmPerSecond * 3.0f,
		HorizontalSpeed);

	FSWShipWakeEvent Event;
	const uint32 OwnerHash = static_cast<uint32>(GetTypeHash(GetOwner()->GetPathName())) & 0x7FFFFFFFu;
	Event.EventId = static_cast<int32>(FMath::Max(OwnerHash, 1u));
	// Origin is the stern source. The bow source is derived as Origin + Forward * HullLength.
	Event.Origin = FVector2D(OwnerLocation) - Forward * SternOffsetCm;
	Event.Forward = Forward;
	Event.UpdateServerTime = Subsystem->GetServerTime();
	Event.Amplitude = MaximumAmplitudeCm * SpeedAlpha * FMath::Clamp(Froude / 0.35f, 0.40f, 1.50f);
	Event.SpeedCmPerSecond = FMath::Max(SmoothedSpectrumSpeed, MinimumSpeedCmPerSecond);
	Event.AdvectionSpeedCmPerSecond = HorizontalSpeed;
	Event.HullLengthCm = SafeHullLength;
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

