#include "SWShipWakeEmitterComponent.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "SWShipWakeSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWShipWakeEmitter, Log, All);

namespace
{
	TAutoConsoleVariable<int32> CVarEmitterDebugLog(
		TEXT("sw.ShipWake.EmitterDebugLog"), 0,
		TEXT("Control Kelvin Wake Emitter debug logging:\n")
		TEXT("  0: Controlled by Component property (bEnableDebugLog)\n")
		TEXT("  1: Force enable debug log for all emitters upon emission\n")
		TEXT("  2: Extra verbose logging (every tick state + emission)"),
		ECVF_Default);

	const TCHAR* GetProfileName(const ESWKelvinFroudeProfile Profile)
	{
		switch (Profile)
		{
		case ESWKelvinFroudeProfile::Fr_0_30: return TEXT("Fr0.30 (Transverse Dominant)");
		case ESWKelvinFroudeProfile::Fr_0_50: return TEXT("Fr0.50 (Balanced Classical)");
		case ESWKelvinFroudeProfile::Fr_0_70: return TEXT("Fr0.70 (Transition Wake)");
		case ESWKelvinFroudeProfile::Fr_1_00: return TEXT("Fr1.00 (Narrow Divergent)");
		default: return TEXT("Unknown");
		}
	}
}

USWShipWakeEmitterComponent::USWShipWakeEmitterComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(true);
}

bool USWShipWakeEmitterComponent::IsDebugLogEnabled() const
{
	const int32 CVarVal = CVarEmitterDebugLog.GetValueOnGameThread();
	return (CVarVal > 0) || bEnableDebugLog;
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

	if (IsDebugLogEnabled())
	{
		const AActor* Owner = GetOwner();
		UE_LOG(LogSWShipWakeEmitter, Log,
			TEXT("[WakeEmitter::BeginPlay] Owner='%s' | InitApex=(%.1f, %.1f) | InitFwd=(%.3f, %.3f) | Profile=%s | ServerTime=%.2fs"),
			Owner ? *Owner->GetName() : TEXT("None"),
			LastSamplePosition.X, LastSamplePosition.Y,
			LastSampleForward.X, LastSampleForward.Y,
			GetProfileName(FroudeProfile), LastSampleServerTime);
	}
}

void USWShipWakeEmitterComponent::ResolveKelvinFrame(
	FVector2D& OutApex, FVector2D& OutForward, const bool bReversing) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		OutApex = FVector2D::ZeroVector;
		OutForward = FVector2D(1.0, 0.0);
		return;
	}
	const FVector ChosenOffset = bReversing ? KelvinSternLocalOffset : KelvinApexLocalOffset;
	const FVector Apex = Owner->GetActorTransform().TransformPosition(ChosenOffset);
	const float Yaw = FMath::DegreesToRadians(KelvinDirectionYawDegrees);
	const float DirectionSign = bReversing ? -1.0f : 1.0f;
	const FVector LocalForward(DirectionSign * FMath::Cos(Yaw), DirectionSign * FMath::Sin(Yaw), 0.0f);
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

	USWShipWakeSubsystem* State = World->GetSubsystem<USWShipWakeSubsystem>();
	if (!State) return;

	const FVector Velocity = Owner->GetVelocity();
	const FVector2D Velocity2D(Velocity.X, Velocity.Y);
	const float Speed = Velocity2D.Size();

	// 선박의 전방 벡터와 수평 속도의 내적으로 후진 여부 판별
	const FVector ActorForward3D = Owner->GetActorForwardVector();
	const FVector2D ActorForward2D = FVector2D(ActorForward3D.X, ActorForward3D.Y).GetSafeNormal();
	const float ForwardVelocityComponent = FVector2D::DotProduct(Velocity2D, ActorForward2D);
	const bool bIsReversing = bAutoReverseWakeOnBackward && (ForwardVelocityComponent < -10.0f);

	FVector2D Apex;
	FVector2D Forward;
	ResolveKelvinFrame(Apex, Forward, bIsReversing);
	if (Forward.IsNearlyZero()) return;
	const double ServerTime = State->GetServerTime();

	// 전진 <-> 후진 방향이 반전되거나 속도가 임계치 미만일 때 샘플 리셋
	const bool bDirectionFlipped = (bLastReversing != bIsReversing);
	bLastReversing = bIsReversing;

	const bool bLogging = IsDebugLogEnabled();
	const int32 VerboseLevel = CVarEmitterDebugLog.GetValueOnGameThread();

	if (bLogging && VerboseLevel >= 2)
	{
		UE_LOG(LogSWShipWakeEmitter, VeryVerbose,
			TEXT("[WakeEmitter::Tick] '%s' Mode=%s Speed=%.1f/%.1f FwdVel=%.1f Rev=%d Flip=%d Apex=(%.1f, %.1f) Fwd=(%.3f, %.3f)"),
			*Owner->GetName(), bAuthority ? TEXT("Auth") : TEXT("Pred"),
			Speed, MinimumSpeedCmPerSecond, ForwardVelocityComponent,
			bIsReversing, bDirectionFlipped, Apex.X, Apex.Y, Forward.X, Forward.Y);
	}

	if (!bHasSample || Speed < MinimumSpeedCmPerSecond || bDirectionFlipped)
	{
		if (bLogging && bDirectionFlipped)
		{
			UE_LOG(LogSWShipWakeEmitter, Log,
				TEXT("[WakeEmitter::Reset] '%s' Direction flipped -> bIsReversing=%d, Speed=%.1f cm/s, Apex=(%.1f, %.1f)"),
				*Owner->GetName(), bIsReversing, Speed, Apex.X, Apex.Y);
		}
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
	const float Spacing = FMath::Max(EmissionDistanceCm, 1.0f);
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

	const bool bLogging = IsDebugLogEnabled();
	if (bLogging)
	{
		const AActor* Owner = GetOwner();
		const TCHAR* TriggerReason = bDistanceTrigger ? TEXT("Distance")
			: (bTurnTrigger ? TEXT("TurnAngle") : TEXT("TimeInterval"));

		UE_LOG(LogSWShipWakeEmitter, Warning,
			TEXT("================================================================================"));
		UE_LOG(LogSWShipWakeEmitter, Warning,
			TEXT("[Kelvin Wake Emission] Owner='%s' | NetRole=%s | Trigger=%s"),
			Owner ? *Owner->GetName() : TEXT("None"),
			bPredicted ? TEXT("Predicted(Client)") : TEXT("Authoritative(Server)"),
			TriggerReason);
		UE_LOG(LogSWShipWakeEmitter, Log,
			TEXT("  >> Kinematics: Speed=%.1f cm/s (Fade=%.2f) | Dist=%.1f cm (Spacing=%.1f) | Turn=%.2f deg (Max=%.1f) | Elapsed=%.3fs (Max=%.3fs)"),
			HorizontalSpeed, SpeedFade, Distance, Spacing, TurnDegrees, MaxTurn, Elapsed, MaximumEmissionInterval);
		UE_LOG(LogSWShipWakeEmitter, Log,
			TEXT("  >> Parameters: Profile=%s | Segments=%d | Lifetime=%.2fs | MaxRadius=%.1f cm | WaveSpeed=%.1f cm/s | Decay=%.4f"),
			GetProfileName(FroudeProfile), SegmentCount, Lifetime, MaximumRadius, PropagationSpeedCmPerSecond, DecayRate);
		UE_LOG(LogSWShipWakeEmitter, Log,
			TEXT("  >> Geometry: Length=%.1f cm | HalfWidth=%.1f cm | EnvelopeWidth=%.1f cm | FadeIn=%.3fs | BaseAmp=%.1f cm"),
			WakeLengthCm, WakeHalfWidthCm, EnvelopeWidthCm, FadeInSeconds, MaximumAmplitudeCm);
	}

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

		if (bLogging)
		{
			UE_LOG(LogSWShipWakeEmitter, Log,
				TEXT("    [Segment %d/%d] Origin=(%.1f, %.1f) | Fwd=(%.3f, %.3f) | StartT=%.3fs | ExpireT=%.3fs | Amp=%.2f cm | Profile=%d"),
				Segment, SegmentCount,
				Event.Origin.X, Event.Origin.Y, Event.Forward.X, Event.Forward.Y,
				Event.StartServerTime, Event.ExpireServerTime,
				Event.InitialAmplitudeCm, static_cast<int32>(Event.FroudeProfile));
		}
	}

	if (bLogging)
	{
		UE_LOG(LogSWShipWakeEmitter, Warning,
			TEXT("================================================================================"));
	}

	LastSamplePosition = Apex;
	LastSampleForward = Forward;
	LastSampleServerTime = ServerTime;
}
