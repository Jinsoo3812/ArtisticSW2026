#include "ShipPhysicsAsync.h"
#include "Chaos/PhysicsObjectInternalInterface.h"
#include "PBDRigidsSolver.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PhysicsObject.h"
#include "Chaos/DebugDrawQueue.h"
#include "HAL/IConsoleManager.h"

FShipPhysicsAsync::FShipPhysicsAsync()
{
}

FShipPhysicsAsync::~FShipPhysicsAsync()
{
}

void FShipPhysicsAsync::OnPostInitialize_Internal()
{
	UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] OnPostInitialize_Internal - Started. PhysicsObject: %s"), PhysicsObject ? TEXT("Valid") : TEXT("Null"));
	if (PhysicsObject)
	{
		Chaos::FWritePhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetWrite();
		if (Chaos::FPBDRigidParticleHandle* ParticleHandle = Interface.GetRigidParticle(PhysicsObject))
		{
			ParticleHandle->SetSleepType(Chaos::ESleepType::NeverSleep);
			UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] OnPostInitialize_Internal - Set SleepType to NeverSleep. ParticleHandle: %s"), ParticleHandle ? TEXT("Valid") : TEXT("Null"));
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

	if (CurrentPhysicsStep % 60 == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] BuildInput_Internal - Step: %d | Move: %.2f | Steer: %.2f"), 
			CurrentPhysicsStep, Input.MovementInput, Input.SteeringInput);
	}
}

void FShipPhysicsAsync::ApplyInput_Internal(const FNetInputShip& Input)
{
	MovementInput_Internal = Input.MovementInput;
	SteeringInput_Internal = Input.SteeringInput;

	if (CurrentPhysicsStep % 60 == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] ApplyInput_Internal - Step: %d | ServerFrame: %d | LocalFrame: %d | Move: %.2f | Steer: %.2f"),
			CurrentPhysicsStep, Input.ServerFrame, Input.LocalFrame, Input.MovementInput, Input.SteeringInput);
		UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT-APPLY] Control-only rewind input | CachedWaves: %d | CachedPontoons: %d | CachedRadius: %.1f"),
			CachedGerstnerWaves.Num(), CachedPontoonOffsets.Num(), CachedBuoyancyRadius);
	}
}

void FShipPhysicsAsync::ValidateInput_Internal(FNetInputShip& Input) const
{
	Input.MovementInput = FMath::Clamp(Input.MovementInput, -1.f, 1.f);
	Input.SteeringInput = FMath::Clamp(Input.SteeringInput, -1.f, 1.f);
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

			// 60틱마다 서버 측 물리 상태 복제 전송 데이터 진단 출력
			if (CurrentPhysicsStep % 60 == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT-STATE-BUILD] Step: %d | ServerFrame: %d | LocalFrame: %d | Pos: %s | Vel: %s"),
					CurrentPhysicsStep, State.ServerFrame, State.LocalFrame, *State.Position.ToString(), *State.LinearVelocity.ToString());
			}
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
			// 물리 롤백 수신 및 강제 롤백 적용 시 실시간 데이터 출력 (필터 해제하여 모든 롤백 실측)
			UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT-CL-APPLY] ApplyState - Step: %d | ServerFrame: %d | LocalFrame: %d | RecvPos: %s | RecvVel: %s | CurPos: %s"),
				CurrentPhysicsStep,
				State.ServerFrame,
				State.LocalFrame,
				*State.Position.ToString(),
				*State.LinearVelocity.ToString(),
				*ParticleHandle->GetX().ToString());

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
			/*if (CurrentPhysicsStep % 60 == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] ProcessInputs_Internal - Step: %d | AsyncInput: %s"), 
					CurrentPhysicsStep, AsyncInput ? TEXT("Valid") : TEXT("Null"));
			}*/

			if (AsyncInput)
			{
				// 로컬 컨트롤러가 있는 피어(로컬 조종사)만 WASD 조종력을 직접 덮어씀.
				// 서버 및 다른 관망 클라이언트는 네트워크 입력 패킷(ApplyInput_Internal)으로 수신된 조종력을 그대로 보존.
				if (AsyncInput->bHasLocalController)
				{
					MovementInput_Internal = AsyncInput->MovementInput;
					SteeringInput_Internal = AsyncInput->SteeringInput;
				}

				// 최초 마샬링 시에만 필요한 폰툰 및 파도 설정 캐싱
				if (AsyncInput->PontoonOffsets.Num() > 0)
				{
					CachedPontoonOffsets = AsyncInput->PontoonOffsets;
				}
				if (AsyncInput->GerstnerWaves.Num() > 0)
				{
					CachedGerstnerWaves = AsyncInput->GerstnerWaves;
				}
				if (AsyncInput->ServerPhysicsTimeOrigin >= 0.0 && AsyncInput->ServerPhysicsStepSeconds > UE_SMALL_NUMBER)
				{
					CachedServerPhysicsTimeOrigin = AsyncInput->ServerPhysicsTimeOrigin;
					CachedServerPhysicsStepSeconds = AsyncInput->ServerPhysicsStepSeconds;
				}
				if (AsyncInput->bNetworkPhysicsTickOffsetAssigned)
				{
					const bool bWasAssigned = bHasNetworkPhysicsTickOffset;
					CachedNetworkPhysicsTickOffset = AsyncInput->NetworkPhysicsTickOffset;
					bHasNetworkPhysicsTickOffset = true;
					if (!bWasAssigned)
					{
						UE_LOG(LogTemp, Warning, TEXT("[NETPHYS-OFFSET] PT accepted synchronized tick offset at LocalStep=%d Offset=%d"),
							CurrentPhysicsStep,
							CachedNetworkPhysicsTickOffset);
					}
				}
				CachedGravityZ = AsyncInput->GravityZ;
				CachedLateralDrag = AsyncInput->LateralDrag;
				CachedForwardForce = AsyncInput->ForwardForceValue;
				CachedTurnTorque = AsyncInput->TurnTorqueValue;
				CachedSpeedMultiplier = AsyncInput->SpeedMultiplier;
				CachedBuoyancyRadius = AsyncInput->BuoyancyRadius;
				CachedBuoyancyForceMultiplier = AsyncInput->BuoyancyForceMultiplier;
				CachedWaterDamping = AsyncInput->WaterDamping;
				CachedWaterDamping2 = AsyncInput->WaterDamping2;
				CachedMaxBuoyantForce = AsyncInput->MaxBuoyantForce;
				CachedResimLocationThreshold = AsyncInput->ResimLocationThreshold;
				CachedResimRotationThreshold = AsyncInput->ResimRotationThreshold;
			}
		}
	}

	// 안전장치: PhysicsObject 유효성 검사 최우선 배치로 레이스 컨디션 차단
	if (!PhysicsObject)
	{
		/*if (CurrentPhysicsStep % 60 == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] ProcessInputs_Internal - Step: %d | PhysicsObject: Null (Skip Simulation)"), CurrentPhysicsStep);
		}*/
		return;
	}

	Chaos::FWritePhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetWrite();
	Chaos::FPBDRigidParticleHandle* ParticleHandle = Interface.GetRigidParticle(PhysicsObject);
	if (!ParticleHandle)
	{
		/*if (CurrentPhysicsStep % 60 == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] ProcessInputs_Internal - Step: %d | ParticleHandle is NULL!"), CurrentPhysicsStep);
		}*/
		return;
	}
	if (ParticleHandle->Disabled())
	{
		/*if (CurrentPhysicsStep % 60 == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] ProcessInputs_Internal - Step: %d | ParticleHandle is DISABLED!"), CurrentPhysicsStep);
		}*/
		return;
	}

	// 60틱 주기 정밀 상태 모니터링 로그
	/*if (CurrentPhysicsStep % 60 == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] ProcessInputs_Internal - Step: %d | PhysicsObject: Valid | IsSleeping: %s | IsResimming: %s"), 
			CurrentPhysicsStep, ParticleHandle->Sleeping() ? TEXT("True") : TEXT("False"), bIsResimming ? TEXT("True") : TEXT("False"));
	}*/

	// 잠자기 방지 설정
	ParticleHandle->SetSleepType(Chaos::ESleepType::NeverSleep);

	const bool bStaticDataReady = CachedPontoonOffsets.Num() > 0
		&& CachedGerstnerWaves.Num() > 0
		&& CachedServerPhysicsTimeOrigin >= 0.0
		&& CachedServerPhysicsStepSeconds > UE_SMALL_NUMBER
		&& bHasNetworkPhysicsTickOffset;

	if (!bStaticDataReady)
	{
		if (CurrentPhysicsStep % 60 == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT-WAITING] Step=%d Pontoons=%d Waves=%d Clock=%s Dt=%.6f FrameOffset=%s"),
				CurrentPhysicsStep,
				CachedPontoonOffsets.Num(),
				CachedGerstnerWaves.Num(),
				CachedServerPhysicsTimeOrigin >= 0.0 ? TEXT("Ready") : TEXT("Missing"),
				CachedServerPhysicsStepSeconds,
				bHasNetworkPhysicsTickOffset ? TEXT("Ready") : TEXT("Missing"));
		}
		return;
	}

	// 서버-클라이언트 간의 실시간 물리/파도 파라미터 Desync 정밀 진단 로그
	if (CurrentPhysicsStep % 60 == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT-PARAMS] Step: %d | IsResim: %s | Waves: %d | Pontoons: %d | Radius: %.2f | Multiplier: %.2f | Damp: %.2f | Damp2: %.2f | MaxForce: %.2f"),
			CurrentPhysicsStep,
			bIsResimming ? TEXT("True") : TEXT("False"),
			CachedGerstnerWaves.Num(),
			CachedPontoonOffsets.Num(),
			CachedBuoyancyRadius,
			CachedBuoyancyForceMultiplier,
			CachedWaterDamping,
			CachedWaterDamping2,
			CachedMaxBuoyantForce);
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
	FVector FirstPontoonWorldPosition = FVector::ZeroVector;
	float FirstPontoonWaveHeight = 0.0f;
	float FirstPontoonDepth = 0.0f;
	float FirstPontoonForceZ = 0.0f;

	/*if (bIsResimming)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] RESIMULATE TICK - Step: %d | SimTime: %.4f | Loc: %s"), 
			CurrentPhysicsStep, SimTime, *ActorLocation.ToString());
	}*/

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
	if (CachedPontoonOffsets.Num() > 0 && CachedGerstnerWaves.Num() > 0)
	{
		float GravityMag = FMath::Abs(CachedGravityZ);

		for (int32 PontoonIndex = 0; PontoonIndex < CachedPontoonOffsets.Num(); ++PontoonIndex)
		{
			const FVector& Offset = CachedPontoonOffsets[PontoonIndex];
			// 폰툰의 월드 좌표 구하기
			FVector PontoonWorldPos = ActorLocation + ActorRotation.RotateVector(Offset);

			// 스레드 세이프 삼각함수 파고 연산 실행
			float WaveHeightZ = GetWaveHeightAtPosition_Internal(PontoonWorldPos, SimTime, CachedGerstnerWaves, GravityMag);

			// 물에 잠긴 깊이 계산
			float Depth = WaveHeightZ - PontoonWorldPos.Z;

			if (CurrentPhysicsStep % 60 == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT-PONTOON] Step: %d | WorldZ: %.2f | WaveZ: %.2f | Depth: %.2f"),
					CurrentPhysicsStep, PontoonWorldPos.Z, WaveHeightZ, Depth);
			}

			float Submersion = Depth + CachedBuoyancyRadius;
			float PontoonForceZ = 0.0f;

			if (Submersion > 0.0f)
			{
				// 구체 폰툰 체적 적분 계산 (Spherical Cap Volume)
				float SubDiff = FMath::Clamp(Submersion, 0.f, 2.f * CachedBuoyancyRadius);
				float SubDiffSq = SubDiff * SubDiff;
				float SubVolume = (PI / 3.f) * SubDiffSq * ((3.f * CachedBuoyancyRadius) - SubDiff);

				// 폰툰 로컬 속도 추출
				FVector PontoonVelocity = ParticleHandle->GetV() + FVector::CrossProduct(ParticleHandle->GetW(), PontoonWorldPos - ActorLocation);
				float VelocityZ = PontoonVelocity.Z;

				// 1차 및 2차 감쇄력 연산 (엔진 순정 공식 복원)
				float FirstOrderDrag = CachedWaterDamping * VelocityZ;
				float SecondOrderDrag = FMath::Sign(VelocityZ) * CachedWaterDamping2 * VelocityZ * VelocityZ;
				float DampingFactorZ = -FMath::Max(FirstOrderDrag + SecondOrderDrag, 0.f);

				// Clamp each physical pontoon independently. The Water plugin's
				// normalized PontoonCoefficient is not applicable here because this
				// custom solver intentionally sums four full spherical-cap volumes.
				PontoonForceZ = FMath::Clamp(
					(SubVolume * CachedBuoyancyForceMultiplier) + DampingFactorZ,
					0.0f,
					CachedMaxBuoyantForce);

				FVector PontoonTotalForce = FVector::UpVector * PontoonForceZ;

				TotalBuoyancyForce += PontoonTotalForce;
				TotalBuoyancyTorque += FVector::CrossProduct(PontoonWorldPos - ActorLocation, PontoonTotalForce);
			}

			if (PontoonIndex == 0)
			{
				FirstPontoonWorldPosition = PontoonWorldPos;
				FirstPontoonWaveHeight = WaveHeightZ;
				FirstPontoonDepth = Depth;
				FirstPontoonForceZ = PontoonForceZ;
			}

		}

		ParticleHandle->AddForce(TotalBuoyancyForce);
		ParticleHandle->AddTorque(TotalBuoyancyTorque);

		/*if (CurrentPhysicsStep % 60 == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] Buoyancy Applied - Step: %d | ForceZ: %.2f | Torq: %s | Pontoons: %d | Waves: %d"),
				CurrentPhysicsStep, TotalBuoyancyForce.Z, *TotalBuoyancyTorque.ToString(), CachedPontoonOffsets.Num(), CachedGerstnerWaves.Num());
		}*/
	}
	else
	{
		/*if (CurrentPhysicsStep % 60 == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] Buoyancy Skipped - Step: %d | Pontoons: %d | Waves: %d"),
				CurrentPhysicsStep, CachedPontoonOffsets.Num(), CachedGerstnerWaves.Num());
		}*/
	}

	// BuoyancyRoot has Chaos gravity enabled. Do not add Mass * Gravity here;
	// doing so applies gravity twice and destabilizes the buoyancy solution.

	// 4. WASD 조작 물리 추진력 적용
	if (FMath::Abs(MovementInput_Internal) > KINDA_SMALL_NUMBER || FMath::Abs(SteeringInput_Internal) > KINDA_SMALL_NUMBER)
	{
		// 전진 힘
		if (FMath::Abs(MovementInput_Internal) > KINDA_SMALL_NUMBER)
		{
			FVector Forward = ActorRotation.GetForwardVector();
			Forward.Z = 0.0f;
			Forward.Normalize();

			AppliedControlForce = Forward * CachedForwardForce * MovementInput_Internal * CachedSpeedMultiplier;
			ParticleHandle->AddForce(AppliedControlForce);
		}

		// 회전 토크
		if (FMath::Abs(SteeringInput_Internal) > KINDA_SMALL_NUMBER)
		{
			AppliedControlTorque = FVector(0.f, 0.f, CachedTurnTorque * SteeringInput_Internal * CachedSpeedMultiplier);
			ParticleHandle->AddTorque(AppliedControlTorque);
		}
	}

	if (CurrentServerPhysicsFrame >= 0 && CurrentServerPhysicsFrame % 30 == 0)
	{
		const FVector LinearVelocity = ParticleHandle->GetV();
		const FVector AngularVelocity = ParticleHandle->GetW();
		UE_LOG(LogTemp, Warning, TEXT("[NETPHYS-DET] SF=%d LF=%d RS=%d T=%.9f PX=%.6f PY=%.6f PZ=%.6f QX=%.9f QY=%.9f QZ=%.9f QW=%.9f VX=%.6f VY=%.6f VZ=%.6f WX=%.6f WY=%.6f WZ=%.6f P0X=%.6f P0Y=%.6f P0Z=%.6f WAVE0=%.6f DEPTH0=%.6f PF0=%.6f BFX=%.6f BFY=%.6f BFZ=%.6f BTX=%.6f BTY=%.6f BTZ=%.6f LFX=%.6f LFY=%.6f LFZ=%.6f CFX=%.6f CFY=%.6f CFZ=%.6f CTX=%.6f CTY=%.6f CTZ=%.6f"),
			CurrentServerPhysicsFrame,
			CurrentPhysicsStep,
			bIsResimming ? 1 : 0,
			SimTimeSeconds,
			ActorLocation.X, ActorLocation.Y, ActorLocation.Z,
			ActorRotation.X, ActorRotation.Y, ActorRotation.Z, ActorRotation.W,
			LinearVelocity.X, LinearVelocity.Y, LinearVelocity.Z,
			AngularVelocity.X, AngularVelocity.Y, AngularVelocity.Z,
			FirstPontoonWorldPosition.X, FirstPontoonWorldPosition.Y, FirstPontoonWorldPosition.Z,
			FirstPontoonWaveHeight, FirstPontoonDepth, FirstPontoonForceZ,
			TotalBuoyancyForce.X, TotalBuoyancyForce.Y, TotalBuoyancyForce.Z,
			TotalBuoyancyTorque.X, TotalBuoyancyTorque.Y, TotalBuoyancyTorque.Z,
			AppliedLateralDragForce.X, AppliedLateralDragForce.Y, AppliedLateralDragForce.Z,
			AppliedControlForce.X, AppliedControlForce.Y, AppliedControlForce.Z,
			AppliedControlTorque.X, AppliedControlTorque.Y, AppliedControlTorque.Z);
	}

	// [PT-RESIM] 서버-클라이언트 롤백 리심 상태 정밀 진단용 60틱 로그
	// [PT-RESIM] 서버-클라이언트 롤백 리심 상태 및 SimTime 시간 위상 대조용 60틱 로그
	if (CurrentPhysicsStep % 60 == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PT-RESIM] Step: %d | ServerFrame: %d | FrameOffset: %d | IsResimming: %s | SimTime: %.6f | Pos: %s | Vel: %s"),
			CurrentPhysicsStep,
			CurrentServerPhysicsFrame,
			CachedNetworkPhysicsTickOffset,
			bIsResimming ? TEXT("TRUE") : TEXT("FALSE"),
			SimTime,
			*ParticleHandle->GetX().ToString(),
			*ParticleHandle->GetV().ToString());
	}
}

void FShipPhysicsAsync::OnPreSimulate_Internal()
{
}

float FShipPhysicsAsync::GetWaveHeightAtPosition_Internal(const FVector& Position, float Time, const TArray<FGerstnerWave>& Waves, float Gravity) const
{
	float TotalZOffset = 0.0f;

	for (const FGerstnerWave& Wave : Waves)
	{
		float WaveTime = Wave.WaveSpeed * Time;
		float WavePosition = FVector2D::DotProduct(FVector2D(Position.X, Position.Y), Wave.WaveVector) - WaveTime + Wave.PhaseOffset;
		float WaveCos = FMath::Cos(WavePosition);
		TotalZOffset += WaveCos * Wave.Amplitude;
	}

	return TotalZOffset;
}
