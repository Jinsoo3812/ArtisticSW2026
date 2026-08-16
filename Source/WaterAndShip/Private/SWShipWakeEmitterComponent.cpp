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

	EmitWakePacket(Location, Forward, HorizontalSpeed);
	LastEmissionPosition = Position;
	LastEmissionServerTime = ServerTime;
	bHasEmissionOrigin = true;
}

void USWShipWakeEmitterComponent::EmitWakePacket(
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
	const uint32 OwnerBits = static_cast<uint32>(GetTypeHash(GetOwner()->GetFName())) & 0x7FFFu;
	Event.EventId = static_cast<int32>((OwnerBits << 16) | (static_cast<uint32>(++LocalSequence) & 0xFFFFu));
	Event.Origin = FVector2D(OwnerLocation) - Forward * SternOffsetCm;
	Event.Forward = Forward;
	Event.StartServerTime = Subsystem->GetServerTime();
	Event.InitialAmplitude = MaximumAmplitudeCm * SpeedAlpha * FMath::Clamp(Froude / 0.55f, 0.35f, 1.35f);
	Event.WaveLength = FMath::Clamp(
		(2.0f * PI * HorizontalSpeed * HorizontalSpeed) / (980.0f * 5.0f),
		300.0f,
		1800.0f);
	Event.PhaseSpeed = FMath::Sqrt(980.0f * Event.WaveLength / (2.0f * PI));
	Event.Lifetime = LifetimeSeconds;
	Event.KelvinHalfAngleRadians = FMath::DegreesToRadians(KelvinHalfAngleDegrees);
	MulticastAddWakeEvent(Event);
}

void USWShipWakeEmitterComponent::MulticastAddWakeEvent_Implementation(const FSWShipWakeEvent& Event)
{
	if (UWorld* World = GetWorld())
	{
		if (USWShipWakeSubsystem* Subsystem = World->GetSubsystem<USWShipWakeSubsystem>())
		{
			Subsystem->AddOrUpdateEvent(Event);
		}
	}
}

