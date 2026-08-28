#include "ShipPhysicsAsync.h"
#include "Chaos/PhysicsObjectInternalInterface.h"
#include "PBDRigidsSolver.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PhysicsObject.h"
#include "Chaos/DebugDrawQueue.h"
#include "HAL/IConsoleManager.h"
#include "Misc/LargeWorldRenderPosition.h"
#include "Water/SWBuoyancyMath.h"

namespace
{
	void BlendWaveBetweenLWCTiles(
		const FGerstnerWave& Wave,
		const FVector& Position,
		float Time,
		float& WaveSin,
		float& WaveCos)
	{
		const FVector TileBorderDistance = FVector(FLargeWorldRenderScalar::GetTileSize() * 0.5) - Position.GetAbs();
		constexpr double BlendZoneWidth = 400.0;
		if (TileBorderDistance.X < BlendZoneWidth || TileBorderDistance.Y < BlendZoneWidth)
		{
			const FVector2D BlendPosition(TileBorderDistance.X, TileBorderDistance.Y);
			const double BlendAlpha = FMath::Clamp(BlendPosition.X / BlendZoneWidth, 0.0, 1.0)
				* FMath::Clamp(BlendPosition.Y / BlendZoneWidth, 0.0, 1.0);
			const float BlendPhase = FVector2D::DotProduct(BlendPosition, Wave.WaveVector) - Wave.WaveSpeed * Time;
			float BlendSin = 0.0f;
			float BlendCos = 0.0f;
			FMath::SinCos(&BlendSin, &BlendCos, BlendPhase);
			WaveSin = FMath::Lerp(BlendSin, WaveSin, BlendAlpha);
			WaveCos = FMath::Lerp(BlendCos, WaveCos, BlendAlpha);
		}
	}

	FVector GetWaveOffset(
		const FGerstnerWave& Wave,
		const FVector& Position,
		float Time,
		FVector& OutNormal,
		float& OutOffset1D)
	{
		const float Phase = FVector2D::DotProduct(FVector2D(Position.X, Position.Y), Wave.WaveVector)
			- Wave.WaveSpeed * Time;
		float WaveSin = 0.0f;
		float WaveCos = 0.0f;
		FMath::SinCos(&WaveSin, &WaveCos, Phase);
		BlendWaveBetweenLWCTiles(Wave, Position, Time, WaveSin, WaveCos);

		OutOffset1D = -Wave.Q * WaveSin;
		OutNormal = FVector(
			WaveSin * Wave.WKA * Wave.Direction.X,
			WaveSin * Wave.WKA * Wave.Direction.Y,
			0.0f);
		return FVector(
			OutOffset1D * Wave.Direction.X,
			OutOffset1D * Wave.Direction.Y,
			WaveCos * Wave.Amplitude);
	}

	float EvaluateFullGerstnerHeight(const FVector& WorldPosition, float Time, const TArray<FGerstnerWave>& Waves)
	{
		float WaveHeight = 0.0f;
		const FVector Position(FLargeWorldRenderPosition(WorldPosition).GetOffset());

		for (const FGerstnerWave& Wave : Waves)
		{
			float FirstOffset1D = 0.0f;
			FVector FirstNormal;
			FVector FirstOffset = GetWaveOffset(Wave, Position, Time, FirstNormal, FirstOffset1D);

			if (Wave.Q != 0.0f)
			{
				constexpr float TwoPi = 2.0f * PI;
				const float Position1D = FVector2D::DotProduct(FVector2D(Position.X, Position.Y), Wave.WaveVector)
					- Wave.WaveSpeed * Time;
				const float MappedPosition1D = Position1D >= 0.0f
					? FMath::Fmod(Position1D, TwoPi)
					: TwoPi - FMath::Abs(FMath::Fmod(Position1D, TwoPi));
				const bool bUsePositiveGuess = MappedPosition1D < PI;
				const FVector GuessPosition = Position + (bUsePositiveGuess ? Wave.Direction * Wave.Q : -Wave.Direction * Wave.Q);

				float SecondOffset1D = 0.0f;
				FVector SecondNormal;
				FVector SecondOffset = GetWaveOffset(Wave, GuessPosition, Time, SecondNormal, SecondOffset1D);
				SecondOffset1D += bUsePositiveGuess ? Wave.Q : -Wave.Q;
				if (!bUsePositiveGuess)
				{
					Swap(FirstOffset, SecondOffset);
					Swap(FirstOffset1D, SecondOffset1D);
				}

				const float Denominator = SecondOffset1D - FirstOffset1D;
				const float Alpha = -FirstOffset1D / (Denominator > 0.0f ? Denominator : 1.0f);
				WaveHeight += FMath::Lerp(FirstOffset.Z, SecondOffset.Z, Alpha);
			}
			else
			{
				WaveHeight += FirstOffset.Z;
			}
		}

		return WaveHeight;
	}
}

FShipPhysicsAsync::FShipPhysicsAsync()
{
	// Preserve the previous PT cache defaults until the first non-resim GT input
	// supplies the component-owned force settings.
	CachedBuoyancyForceSettings.BuoyancyCoefficient = 1.2f;
	CachedBuoyancyForceSettings.DeepWaterBuoyancyMultiplier = 1.0f;
	CachedBuoyancyForceSettings.BuoyancyDamp = 3.0f;
	CachedBuoyancyForceSettings.BuoyancyDamp2 = 0.1f;
	CachedBuoyancyForceSettings.MaxBuoyantForce = 5000000.0f;
}

FShipPhysicsAsync::~FShipPhysicsAsync()
{
}

void FShipPhysicsAsync::OnPostInitialize_Internal()
{
	if (PhysicsObject)
	{
		Chaos::FWritePhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetWrite();
		if (Chaos::FPBDRigidParticleHandle* ParticleHandle = Interface.GetRigidParticle(PhysicsObject))
		{
			ParticleHandle->SetSleepType(Chaos::ESleepType::NeverSleep);
		}
	}
}

void FShipPhysicsAsync::OnPhysicsObjectUnregistered_Internal(Chaos::FConstPhysicsObjectHandle InPhysicsObject)
{
	if (PhysicsObject == InPhysicsObject)
	{
		PhysicsObject = nullptr;
	}
}

void FShipPhysicsAsync::BuildInput_Internal(FNetInputShip& Input) const
{
	Input.MovementInput = MovementInput_Internal;
	Input.SteeringInput = SteeringInput_Internal;
	Input.ExternalAcceleration = ExternalAcceleration_Internal;
	Input.bBuoyancyEnabled = bBuoyancyEnabled_Internal;
	Input.bHasAuthoritativeBuoyancyState = bAuthoritativeBuoyancyWriter_Internal;
	Input.bIsAnchorDropped = bAnchorDropped_Internal;
	Input.AnchorOriginXY = CachedAnchorOriginXY;
}

void FShipPhysicsAsync::ApplyInput_Internal(const FNetInputShip& Input)
{
	MovementInput_Internal = Input.MovementInput;
	SteeringInput_Internal = Input.SteeringInput;
	ExternalAcceleration_Internal = Input.ExternalAcceleration;
	if (Input.bHasAuthoritativeBuoyancyState)
	{
		bBuoyancyEnabled_Internal = Input.bBuoyancyEnabled;
	}
	bAnchorDropped_Internal = Input.bIsAnchorDropped;
	CachedAnchorOriginXY = Input.AnchorOriginXY;
}

void FShipPhysicsAsync::ValidateInput_Internal(FNetInputShip& Input) const
{
	Input.MovementInput = FMath::Clamp(Input.MovementInput, -1.f, 1.f);
	Input.SteeringInput = FMath::Clamp(Input.SteeringInput, -1.f, 1.f);
	if (Input.bIsAnchorDropped)
	{
		Input.MovementInput = 0.0f;
		Input.SteeringInput = 0.0f;
	}
	if (Input.ExternalAcceleration.ContainsNaN())
	{
		Input.ExternalAcceleration = FVector::ZeroVector;
	}
	Input.ExternalAcceleration.Z = 0.0f;
	Input.ExternalAcceleration = Input.ExternalAcceleration.GetClampedToMaxSize(5000.f);
}

void FShipPhysicsAsync::BuildState_Internal(FNetStatePhysicsShip& State) const
{
	if (PhysicsObject)
	{
		Chaos::FWritePhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetWrite();
		if (Chaos::FPBDRigidParticleHandle* ParticleHandle = Interface.GetRigidParticle(PhysicsObject))
		{
			State.Position = ParticleHandle->GetX();
			State.Rotation = ParticleHandle->GetR();
			State.LinearVelocity = ParticleHandle->GetV();
			State.AngularVelocity = ParticleHandle->GetW();
			State.LocationThresholdSq = FMath::Square(CachedResimLocationThreshold);
			State.RotationThresholdRad = FMath::DegreesToRadians(CachedResimRotationThreshold);
		}
	}
}

void FShipPhysicsAsync::ApplyState_Internal(const FNetStatePhysicsShip& State)
{
	if (PhysicsObject)
	{
		Chaos::FWritePhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetWrite();
		if (Chaos::FPBDRigidParticleHandle* ParticleHandle = Interface.GetRigidParticle(PhysicsObject))
		{
			ParticleHandle->SetX(State.Position);
			ParticleHandle->SetR(State.Rotation);
			ParticleHandle->SetP(State.Position);
			ParticleHandle->SetQ(State.Rotation);
			ParticleHandle->SetV(State.LinearVelocity);
			ParticleHandle->SetW(State.AngularVelocity);
		}
	}
}

void FShipPhysicsAsync::ProcessInputs_Internal(int32 PhysicsStep)
{
	bool bIsResimming = false;

	if (Chaos::FPhysicsSolverBase* CurrentSolver = GetSolver())
	{
		bIsResimming = CurrentSolver->IsResimming();
		CurrentPhysicsStep = PhysicsStep;
		if (!bIsResimming)
		{
			const FAsyncInputShip* AsyncInput = GetConsumerInput_Internal();
			if (AsyncInput)
			{
				bQueryDiagnostics_Internal = AsyncInput->bQueryDiagnostics;
				// 로컬 컨트롤러가 있는 피어(로컬 조종사)만 WASD 조종력을 직접 덮어씀.
				// 서버 및 다른 관망 클라이언트는 네트워크 입력 패킷(ApplyInput_Internal)으로 수신된 조종력을 그대로 보존.
				if (AsyncInput->bHasLocalController)
				{
					MovementInput_Internal = AsyncInput->MovementInput;
					SteeringInput_Internal = AsyncInput->SteeringInput;
				}
				// Gameplay-authored forces are authoritative and must also
				// reach server-controlled AI ships, which have no local controller.
				if (AsyncInput->bApplyAuthoritativeExternalAcceleration)
				{
					ExternalAcceleration_Internal = AsyncInput->ExternalAcceleration;
				}
				if (AsyncInput->bApplyAuthoritativeBuoyancyState)
				{
					bBuoyancyEnabled_Internal = AsyncInput->bBuoyancyEnabled;
					bAuthoritativeBuoyancyWriter_Internal = true;
				}

				bAnchorDropped_Internal = AsyncInput->bIsAnchorDropped;
				CachedAnchorOriginXY = AsyncInput->AnchorOriginXY;
				CachedAnchorStiffness = AsyncInput->AnchorStiffness;
				CachedAnchorDamping = AsyncInput->AnchorDamping;
				CachedAnchorSlackRadius = AsyncInput->AnchorSlackRadius;
				CachedMaxAnchorForce = AsyncInput->MaxAnchorForce;

				// 최초 마샬링 시에만 필요한 폰툰 및 파도 설정 캐싱
				if (AsyncInput->PontoonOffsets.Num() > 0)
				{
					CachedPontoonOffsets = AsyncInput->PontoonOffsets;
					CachedPontoonRadii = AsyncInput->PontoonRadii;
					CachedPontoonForceScales = AsyncInput->PontoonForceScales;
				}
				if (AsyncInput->GerstnerWaves.Num() > 0)
				{
					CachedGerstnerWaves = AsyncInput->GerstnerWaves;
				}
				CachedRippleEvents = AsyncInput->RippleEvents;
				CachedShipWakeEvents = AsyncInput->ShipWakeEvents;
				if (AsyncInput->ServerPhysicsTimeOrigin >= 0.0 && AsyncInput->ServerPhysicsStepSeconds > UE_SMALL_NUMBER)
				{
					CachedServerPhysicsTimeOrigin = AsyncInput->ServerPhysicsTimeOrigin;
					CachedServerPhysicsStepSeconds = AsyncInput->ServerPhysicsStepSeconds;
				}
				if (AsyncInput->bNetworkPhysicsTickOffsetAssigned)
				{
					CachedNetworkPhysicsTickOffset = AsyncInput->NetworkPhysicsTickOffset;
					bHasNetworkPhysicsTickOffset = true;
				}
				CachedGravityZ = AsyncInput->GravityZ;
				CachedLateralDrag = AsyncInput->LateralDrag;
				CachedForwardForce = AsyncInput->ForwardForceValue;
				CachedTurnTorque = AsyncInput->TurnTorqueValue;
				CachedForwardPropulsionMultiplier = AsyncInput->ForwardPropulsionMultiplier;
				CachedTurnTorqueMultiplier = AsyncInput->TurnTorqueMultiplier;
				CachedBuoyancyRadius = AsyncInput->BuoyancyRadius;
				CachedBuoyancyForceSettings = AsyncInput->BuoyancyForceSettings;
				CachedResimLocationThreshold = AsyncInput->ResimLocationThreshold;
				CachedResimRotationThreshold = AsyncInput->ResimRotationThreshold;
			}
		}
	}

	// 안전장치: PhysicsObject 유효성 검사 최우선 배치로 레이스 컨디션 차단
	if (!PhysicsObject)
	{
		return;
	}

	Chaos::FWritePhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetWrite();
	Chaos::FPBDRigidParticleHandle* ParticleHandle = Interface.GetRigidParticle(PhysicsObject);
	if (!ParticleHandle)
	{
		return;
	}
	if (ParticleHandle->Disabled())
	{
		return;
	}

	// 잠자기 방지 설정
	ParticleHandle->SetSleepType(Chaos::ESleepType::NeverSleep);

	const bool bStaticDataReady = CachedPontoonOffsets.Num() > 0
		&& CachedPontoonRadii.Num() == CachedPontoonOffsets.Num()
		&& CachedGerstnerWaves.Num() > 0
		&& CachedServerPhysicsTimeOrigin >= 0.0
		&& CachedServerPhysicsStepSeconds > UE_SMALL_NUMBER
		&& bHasNetworkPhysicsTickOffset;

	if (!bStaticDataReady)
	{
		return;
	}

	// One authoritative mapping drives both normal simulation and rewind.
	const int32 CurrentServerPhysicsFrame = CurrentPhysicsStep + CachedNetworkPhysicsTickOffset;
	const double SimTimeSeconds = CachedServerPhysicsTimeOrigin
		+ static_cast<double>(CurrentServerPhysicsFrame) * static_cast<double>(CachedServerPhysicsStepSeconds);
	const float SimTime = static_cast<float>(SimTimeSeconds);

	FVector ActorLocation = ParticleHandle->GetX();
	FQuat ActorRotation = ParticleHandle->GetR();
	FVector AppliedLateralDragForce = FVector::ZeroVector;
	FVector TotalBuoyancyForce = FVector::ZeroVector;
	FVector TotalBuoyancyTorque = FVector::ZeroVector;
	FVector AppliedControlForce = FVector::ZeroVector;
	FVector AppliedControlTorque = FVector::ZeroVector;

	// 2. 가로축 수력 드래그 (Lateral Hydrodynamic Drag) 계산 및 적용
	if (CachedLateralDrag > 0.0f)
	{
		FVector Velocity = ParticleHandle->GetV();
		FVector Right = ActorRotation.GetRightVector();
		Right.Z = 0.0f;
		Right.Normalize();

		float LateralSpeed = FVector::DotProduct(Velocity, Right);
		AppliedLateralDragForce = -Right * LateralSpeed * CachedLateralDrag;
		
		ParticleHandle->AddForce(AppliedLateralDragForce);
	}

	// 3. 커스텀 폰툰 기반 부력 연산 (Archimedes Buoyancy & Damping)
	if (bBuoyancyEnabled_Internal
		&& CachedPontoonOffsets.Num() > 0
		&& CachedGerstnerWaves.Num() > 0)
	{
		float GravityMag = FMath::Abs(CachedGravityZ);

		for (int32 PontoonIndex = 0; PontoonIndex < CachedPontoonOffsets.Num(); ++PontoonIndex)
		{
			const FVector& Offset = CachedPontoonOffsets[PontoonIndex];
			// 폰툰의 월드 좌표 구하기
			FVector PontoonWorldPos = ActorLocation + ActorRotation.RotateVector(Offset);

			// 스레드 세이프 삼각함수 파고 연산 실행
			float WaveHeightZ = GetWaveHeightAtPosition_Internal(PontoonWorldPos, SimTime, CachedGerstnerWaves, GravityMag);
			if (bQueryDiagnostics_Internal && PontoonIndex == 0)
			{
				FAsyncOutputShip& Output = GetProducerOutputData_Internal();
				Output.bWaveSampleValid = true;
				Output.bWasResimming = bIsResimming;
				Output.WaveSamplePosition = PontoonWorldPos;
				Output.WaveSampleServerTime = SimTimeSeconds;
				Output.PTWaveHeight = WaveHeightZ;
			}

			const FVector PontoonVelocity = ParticleHandle->GetV()
				+ FVector::CrossProduct(ParticleHandle->GetW(), PontoonWorldPos - ActorLocation);
			FSWBuoyancySolveInput SolveInput;
			SolveInput.WaterHeight = WaveHeightZ;
			SolveInput.PontoonCenterZ = PontoonWorldPos.Z;
			SolveInput.PontoonRadius = CachedPontoonRadii[PontoonIndex];
			SolveInput.RelativeVelocityZ = PontoonVelocity.Z;
			SolveInput.ForceScale = CachedPontoonForceScales.IsValidIndex(PontoonIndex)
				? CachedPontoonForceScales[PontoonIndex]
				: 1.0f;
			const FSWBuoyancySolveResult SolveResult =
				FSWBuoyancyMath::SolvePontoon(SolveInput, CachedBuoyancyForceSettings);
			const float PontoonForceZ = SolveResult.BuoyantForceZ;

			if (PontoonForceZ > 0.0f)
			{
				FVector PontoonTotalForce = FVector::UpVector * PontoonForceZ;

				TotalBuoyancyForce += PontoonTotalForce;
				TotalBuoyancyTorque += FVector::CrossProduct(PontoonWorldPos - ActorLocation, PontoonTotalForce);
			}
		}

		ParticleHandle->AddForce(TotalBuoyancyForce);
		ParticleHandle->AddTorque(TotalBuoyancyTorque);
	}

	// 4. 닻(Anchor) 수평 평면 스프링-댐퍼 저항 적용 (Z축 파도/부력에는 무영향)
	if (bAnchorDropped_Internal)
	{
		MovementInput_Internal = 0.0f;
		SteeringInput_Internal = 0.0f;

		const FVector CurrentVelocity = ParticleHandle->GetV();
		const FVector PlanarVelocity = FVector(CurrentVelocity.X, CurrentVelocity.Y, 0.0f);

		FVector TotalAnchorForce = -PlanarVelocity * CachedAnchorDamping;

		// 닻 고정점 위치가 유효한 경우 복원 스프링 힘 추가 (외부 충격 방어)
		if (!CachedAnchorOriginXY.IsNearlyZero(0.1f))
		{
			const FVector PlanarDisplacement = FVector(ActorLocation.X - CachedAnchorOriginXY.X, ActorLocation.Y - CachedAnchorOriginXY.Y, 0.0f);
			const float DistFromAnchor = PlanarDisplacement.Size();
			if (DistFromAnchor > CachedAnchorSlackRadius)
			{
				const FVector RestoringDir = PlanarDisplacement.GetSafeNormal();
				const float EffectiveDist = DistFromAnchor - CachedAnchorSlackRadius;
				const FVector SpringForce = -RestoringDir * EffectiveDist * CachedAnchorStiffness;
				TotalAnchorForce += SpringForce;
			}
		}

		if (CachedMaxAnchorForce > 0.0f)
		{
			TotalAnchorForce = TotalAnchorForce.GetClampedToMaxSize(CachedMaxAnchorForce);
		}

		ParticleHandle->AddForce(TotalAnchorForce);
	}

	// 5. WASD 조작 물리 추진력 적용 (닻이 내려져 있지 않을 때만 적용)
	if (!bAnchorDropped_Internal && (FMath::Abs(MovementInput_Internal) > KINDA_SMALL_NUMBER || FMath::Abs(SteeringInput_Internal) > KINDA_SMALL_NUMBER))
	{
		// 전진 힘
		if (FMath::Abs(MovementInput_Internal) > KINDA_SMALL_NUMBER)
		{
			FVector Forward = ActorRotation.GetForwardVector();
			Forward.Z = 0.0f;
			Forward.Normalize();

			AppliedControlForce = Forward * CachedForwardForce * MovementInput_Internal * CachedForwardPropulsionMultiplier;
			ParticleHandle->AddForce(AppliedControlForce);
		}

		// 회전 토크
		if (FMath::Abs(SteeringInput_Internal) > KINDA_SMALL_NUMBER)
		{
			AppliedControlTorque = FVector(0.f, 0.f, CachedTurnTorque * SteeringInput_Internal * CachedTurnTorqueMultiplier);
			ParticleHandle->AddTorque(AppliedControlTorque);
		}
	}

	// Replay gameplay-authored acceleration through the same Network Physics
	// input history as steering and throttle.
	if (!ExternalAcceleration_Internal.IsNearlyZero())
	{
		const float ParticleMass = ParticleHandle->M();
		if (ParticleMass > UE_SMALL_NUMBER)
		{
			ParticleHandle->AddForce(ExternalAcceleration_Internal * ParticleMass);
		}
	}
}

void FShipPhysicsAsync::OnPreSimulate_Internal()
{
}

float FShipPhysicsAsync::GetWaveHeightAtPosition_Internal(const FVector& Position, float Time, const TArray<FGerstnerWave>& Waves, float Gravity) const
{
	// Match UGerstnerWaterWaves::GetWaveHeightAtPosition, including the
	// horizontal-displacement inversion used when steepness (Q) is non-zero.
	float TotalZOffset = EvaluateFullGerstnerHeight(Position, Time, Waves);

	TotalZOffset += FSWRippleEvaluator::EvaluateHeight(
		FVector2D(Position.X, Position.Y),
		static_cast<double>(Time),
		CachedRippleEvents);

	TotalZOffset += FSWShipWakeEvaluator::EvaluateHeight(
		FVector2D(Position.X, Position.Y),
		static_cast<double>(Time),
		CachedShipWakeEvents);

	return TotalZOffset;
}
