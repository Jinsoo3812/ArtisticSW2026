#include "SWShipWakeEmitterComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Ship.h"
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

bool USWShipWakeEmitterComponent::IsEnemyShip() const
{
	if (bIsEnemyShip) return true;
	if (bAutoDetectEnemyShip)
	{
		if (const AActor* Owner = GetOwner())
		{
			if (Owner->ActorHasTag(TEXT("Enemy")) || Owner->GetClass()->GetName().Contains(TEXT("Enemy")))
			{
				return true;
			}
			if (const AShip* Ship = Cast<AShip>(Owner))
			{
				if (Ship->IsEnemyShipForEffects()) return true;
			}
		}
	}
	return false;
}

bool USWShipWakeEmitterComponent::IsCulledByCamera(const FVector2D& Apex) const
{
	if (MaxCameraCullingDistanceCm <= 0.0f) return false;
	const UWorld* World = GetWorld();
	if (!World) return false;
	if (World->GetNetMode() == NM_DedicatedServer) return false;

	FVector CameraLocation = FVector::ZeroVector;
	bool bFoundCamera = false;

	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		if (const APlayerCameraManager* CamMgr = PC->PlayerCameraManager)
		{
			CameraLocation = CamMgr->GetCameraLocation();
			bFoundCamera = true;
		}
		else if (const APawn* PlayerPawn = PC->GetPawn())
		{
			CameraLocation = PlayerPawn->GetActorLocation();
			bFoundCamera = true;
		}
	}

	if (bFoundCamera)
	{
		const float DistSq = FVector2D::DistSquared(Apex, FVector2D(CameraLocation.X, CameraLocation.Y));
		return DistSq > FMath::Square(MaxCameraCullingDistanceCm);
	}
	return false;
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
			TEXT("[WakeEmitter::BeginPlay] Owner='%s' (Enemy=%d) | InitApex=(%.1f, %.1f) | InitFwd=(%.3f, %.3f) | Profile=%s | ServerTime=%.2fs"),
			Owner ? *Owner->GetName() : TEXT("None"),
			IsEnemyShip(),
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
	const bool bIsLocallyControlledPlayer = Pawn && Pawn->IsLocallyControlled();

	// PlayerShip과 완벽히 동일한 네트워크 실행 파이프라인:
	// - 서버/로컬 권한(bAuthority): Standalone, ListenServer, Server 모두 정상 실행 (복제 이벤트 생성)
	// - 클라이언트(!bAuthority): bEnableClientPrediction이 켜져 있으면 로컬 플레이어/적 배 모두 로컬 예측으로 즉시 생성
	const bool bPredicted = !bAuthority && bEnableClientPrediction;

	if (!bAuthority && !bPredicted) return;

	USWShipWakeSubsystem* State = World->GetSubsystem<USWShipWakeSubsystem>();
	if (!State) return;

	const FVector Velocity = Owner->GetVelocity();
	const FVector2D Velocity2D(Velocity.X, Velocity.Y);
	float Speed = Velocity2D.Size();

	// 위치 차이 기반 속도 계산 보조 (특수 AI 이동 등으로 Velocity가 0인 경우 대비)
	if (Speed < 1.0f && bHasSample && DeltaTime > UE_SMALL_NUMBER)
	{
		const float FrameDist = FVector2D::Distance(LastSamplePosition, FVector2D(Owner->GetActorLocation()));
		Speed = FrameDist / DeltaTime;
	}

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

	// 카메라 거리 컬링 검사 (플레이어 카메라와 멀면 생성 스킵)
	if (IsCulledByCamera(Apex))
	{
		LastSamplePosition = Apex;
		LastSampleForward = Forward;
		LastSampleServerTime = ServerTime;
		bHasSample = true;
		return;
	}

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
			TEXT("  >> Geometry: Length=%.1f cm | HalfWidth=%.1f cm | CutRatio=(Len:%.2f, Wid:%.2f) | EnvelopeWidth=%.1f cm | FadeIn=%.3fs | BaseAmp=%.1f cm"),
			WakeLengthCm, WakeHalfWidthCm, LengthCutRatio, WidthCutRatio, EnvelopeWidthCm, FadeInSeconds, MaximumAmplitudeCm);
	}

	for (int32 Segment = 1; Segment <= SegmentCount; ++Segment)
	{
		const float Alpha0 = static_cast<float>(Segment - 1) / SegmentCount;
		const float Alpha1 = static_cast<float>(Segment) / SegmentCount;

		FVector2D Tangent0 = FMath::Lerp(LastSampleForward, Forward, Alpha0);
		Tangent0 = Tangent0.IsNearlyZero() ? LastSampleForward : Tangent0.GetSafeNormal();
		FVector2D Tangent1 = FMath::Lerp(LastSampleForward, Forward, Alpha1);
		Tangent1 = Tangent1.IsNearlyZero() ? Forward : Tangent1.GetSafeNormal();

		FSWShipWakeEvent Event;
		Event.Origin = FMath::Lerp(LastSamplePosition, Apex, Alpha0);
		Event.EndOrigin = FMath::Lerp(LastSamplePosition, Apex, Alpha1);
		Event.Forward = Tangent0;
		Event.EndForward = Tangent1;
		Event.StartServerTime = FMath::Lerp(LastSampleServerTime, ServerTime, Alpha0);
		Event.EndServerTime = FMath::Lerp(LastSampleServerTime, ServerTime, Alpha1);
		Event.ExpireServerTime = Event.EndServerTime + Lifetime;
		Event.InitialAmplitudeCm = MaximumAmplitudeCm * SpeedFade;
		Event.PropagationSpeedCmPerSecond = FMath::Max(PropagationSpeedCmPerSecond, 1.0f);
		Event.DecayRate = FMath::Max(DecayRate, 0.0f);
		Event.WakeLengthCm = FMath::Max(WakeLengthCm, 100.0f);
		Event.WakeHalfWidthCm = FMath::Max(WakeHalfWidthCm, 100.0f);
		Event.LengthCutRatio = FMath::Clamp(LengthCutRatio, 0.01f, 1.0f);
		Event.WidthCutRatio = FMath::Clamp(WidthCutRatio, 0.01f, 1.0f);
		Event.EnvelopeWidthCm = FMath::Max(EnvelopeWidthCm, 10.0f);
		Event.FadeInSeconds = FMath::Max(FadeInSeconds, 0.0f);
		Event.FroudeProfile = FroudeProfile;

		if (bPredicted) State->SubmitPredictedEvent(Event);
		else State->SubmitAuthoritativeEvent(Event);

		if (bLogging)
		{
			UE_LOG(LogSWShipWakeEmitter, Log,
				TEXT("    [Segment %d/%d] P0=(%.1f, %.1f) P1=(%.1f, %.1f) | Fwd0=(%.2f, %.2f) Fwd1=(%.2f, %.2f) | T=[%.2f..%.2f] Exp=%.2f | Amp=%.1f | Profile=%d"),
				Segment, SegmentCount,
				Event.Origin.X, Event.Origin.Y, Event.EndOrigin.X, Event.EndOrigin.Y,
				Event.Forward.X, Event.Forward.Y, Event.EndForward.X, Event.EndForward.Y,
				Event.StartServerTime, Event.EndServerTime, Event.ExpireServerTime,
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
