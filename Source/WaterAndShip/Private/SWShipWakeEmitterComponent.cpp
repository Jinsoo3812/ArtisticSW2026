#include "SWShipWakeEmitterComponent.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
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
	ResolveKelvinFrame(LastSamplePosition, LastSampleForward);
	if (const USWShipWakeSubsystem* State = GetWorld()
		? GetWorld()->GetSubsystem<USWShipWakeSubsystem>() : nullptr)
	{
		LastSampleServerTime = State->GetServerTime();
	}
	bHasSample = true;
}

void USWShipWakeEmitterComponent::ResolveKelvinFrame(
	FVector2D& OutApex, FVector2D& OutForward) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		OutApex = FVector2D::ZeroVector;
		OutForward = FVector2D(1.0, 0.0);
		return;
	}
	const FVector Apex = Owner->GetActorTransform().TransformPosition(KelvinApexLocalOffset);
	const float Yaw = FMath::DegreesToRadians(KelvinDirectionYawDegrees);
	const FVector LocalForward(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.0f);
	const FVector WorldForward = Owner->GetActorTransform().TransformVectorNoScale(LocalForward);
	OutApex = FVector2D(Apex);
	OutForward = FVector2D(WorldForward).GetSafeNormal();
}

void USWShipWakeEmitterComponent::TickComponent(
	const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World) return;

	const bool bAuthority = Owner->HasAuthority();
	const APawn* Pawn = Cast<APawn>(Owner);
	const bool bPredicted = !bAuthority && bEnableClientPrediction && Pawn && Pawn->IsLocallyControlled();
	if (!bAuthority && !bPredicted) return;

	FVector2D Apex;
	FVector2D Forward;
	ResolveKelvinFrame(Apex, Forward);
	USWShipWakeSubsystem* State = World->GetSubsystem<USWShipWakeSubsystem>();
	if (!State || Forward.IsNearlyZero()) return;
	const double ServerTime = State->GetServerTime();
	const FVector Velocity = Owner->GetVelocity();
	const float Speed = FVector2D(Velocity.X, Velocity.Y).Size();

	if (!bHasSample || Speed < MinimumSpeedCmPerSecond)
	{
		LastSamplePosition = Apex;
		LastSampleForward = Forward;
		LastSampleServerTime = ServerTime;
		bHasSample = true;
		return;
	}
	EmitResampledSegment(Apex, Forward, Speed, ServerTime, bPredicted);
}

void USWShipWakeEmitterComponent::EmitResampledSegment(
	const FVector2D& Apex, const FVector2D& Forward,
	const float HorizontalSpeed, const double ServerTime, const bool bPredicted)
{
	USWShipWakeSubsystem* State = GetWorld()->GetSubsystem<USWShipWakeSubsystem>();
	if (!State) return;

	const float Distance = FVector2D::Distance(LastSamplePosition, Apex);
	const float Dot = FMath::Clamp(
		static_cast<float>(FVector2D::DotProduct(LastSampleForward, Forward)), -1.0f, 1.0f);
	const float TurnDegrees = FMath::RadiansToDegrees(FMath::Acos(Dot));
	const float Spacing = FMath::Max(EmissionDistanceCm, 50.0f);
	const float MaxTurn = FMath::Max(MaximumTurnAngleDegrees, 1.0f);
	const double Elapsed = ServerTime - LastSampleServerTime;
	const bool bDistanceTrigger = Distance >= Spacing;
	const bool bTurnTrigger = Distance >= Spacing * 0.25f && TurnDegrees >= MaxTurn;
	const bool bTimeTrigger = Distance >= Spacing * 0.25f && Elapsed >= MaximumEmissionInterval;
	if (!bDistanceTrigger && !bTurnTrigger && !bTimeTrigger) return;

	const int32 SegmentCount = FMath::Clamp(FMath::CeilToInt(FMath::Max3(
		Distance / Spacing, TurnDegrees / MaxTurn, 1.0f)), 1, MaximumCatchUpEvents);
	const float SpeedFade = FMath::SmoothStep(
		MinimumSpeedCmPerSecond, MinimumSpeedCmPerSecond * 2.0f, HorizontalSpeed);
	const float MaximumRadius = FMath::Sqrt(
		FMath::Square(WakeLengthCm) + FMath::Square(WakeHalfWidthCm));
	const float Lifetime = FMath::Max(
		(MaximumRadius + EnvelopeWidthCm) / FMath::Max(PropagationSpeedCmPerSecond, 1.0f), 1.0f);

	for (int32 Segment = 1; Segment <= SegmentCount; ++Segment)
	{
		const float Alpha = static_cast<float>(Segment) / SegmentCount;
		FVector2D Tangent = FMath::Lerp(LastSampleForward, Forward, Alpha);
		Tangent = Tangent.IsNearlyZero() ? Forward : Tangent.GetSafeNormal();
		FSWShipWakeEvent Event;
		Event.Origin = FMath::Lerp(LastSamplePosition, Apex, Alpha);
		Event.Forward = Tangent;
		Event.StartServerTime = FMath::Lerp(LastSampleServerTime, ServerTime, Alpha);
		Event.ExpireServerTime = Event.StartServerTime + Lifetime;
		Event.InitialAmplitudeCm = MaximumAmplitudeCm * SpeedFade;
		Event.PropagationSpeedCmPerSecond = FMath::Max(PropagationSpeedCmPerSecond, 1.0f);
		Event.DecayRate = FMath::Max(DecayRate, 0.0f);
		Event.WakeLengthCm = FMath::Max(WakeLengthCm, 100.0f);
		Event.WakeHalfWidthCm = FMath::Max(WakeHalfWidthCm, 100.0f);
		Event.EnvelopeWidthCm = FMath::Max(EnvelopeWidthCm, 10.0f);
		Event.FadeInSeconds = FMath::Max(FadeInSeconds, 0.0f);
		Event.FroudeProfile = FroudeProfile;
		if (bPredicted) State->SubmitPredictedEvent(Event);
		else State->SubmitAuthoritativeEvent(Event);
	}

	LastSamplePosition = Apex;
	LastSampleForward = Forward;
	LastSampleServerTime = ServerTime;
}
