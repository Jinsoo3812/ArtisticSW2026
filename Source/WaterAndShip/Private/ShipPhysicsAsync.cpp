#include "ShipPhysicsAsync.h"
#include "Chaos/PhysicsObjectInternalInterface.h"
#include "PBDRigidsSolver.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PhysicsObject.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"


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
	
	Input.PontoonOffsets = CachedPontoonOffsets;
	Input.GerstnerWaves = CachedGerstnerWaves;
	Input.GravityZ = CachedGravityZ;
	Input.LateralDrag = CachedLateralDrag;
	Input.ForwardForceValue = CachedForwardForce;
	Input.TurnTorqueValue = CachedTurnTorque;
	Input.SpeedMultiplier = CachedSpeedMultiplier;
	Input.BuoyancyRadius = CachedBuoyancyRadius;
	Input.BuoyancyForceMultiplier = CachedBuoyancyForceMultiplier;
	Input.WaterDamping = CachedWaterDamping;
	Input.WaterDamping2 = CachedWaterDamping2;
	Input.SpawnWorldTime = CachedSpawnWorldTime;
	Input.SpawnPhysicsStep = CachedSpawnPhysicsStep;

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

	CachedPontoonOffsets = Input.PontoonOffsets;
	CachedGerstnerWaves = Input.GerstnerWaves;
	CachedGravityZ = Input.GravityZ;
	CachedLateralDrag = Input.LateralDrag;
	CachedForwardForce = Input.ForwardForceValue;
	CachedTurnTorque = Input.TurnTorqueValue;
	CachedSpeedMultiplier = Input.SpeedMultiplier;
	CachedBuoyancyRadius = Input.BuoyancyRadius;
	CachedBuoyancyForceMultiplier = Input.BuoyancyForceMultiplier;
	CachedWaterDamping = Input.WaterDamping;
	CachedWaterDamping2 = Input.WaterDamping2;
	CachedSpawnWorldTime = Input.SpawnWorldTime;
	CachedSpawnPhysicsStep = Input.SpawnPhysicsStep;

	if (CurrentPhysicsStep % 60 == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] ApplyInput_Internal - Step: %d | Move: %.2f | Steer: %.2f"), 
			CurrentPhysicsStep, Input.MovementInput, Input.SteeringInput);
		UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT-APPLY] Received Input - Waves: %d | Pontoons: %d | BuoyancyRadius: %.1f"), 
			Input.GerstnerWaves.Num(), Input.PontoonOffsets.Num(), Input.BuoyancyRadius);
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

			// 60틱마다 서버 측 물리 상태 복제 전송 데이터 진단 출력
			if (CurrentPhysicsStep % 60 == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT-SRV] BuildState - Step: %d | Pos: %s | Vel: %s"), 
					CurrentPhysicsStep, *State.Position.ToString(), *State.LinearVelocity.ToString());
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
			// 60틱마다 클라이언트 측 물리 롤백 수신 데이터 진단 출력
			if (CurrentPhysicsStep % 60 == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT-CL] ApplyState - Step: %d | RecvPos: %s | RecvVel: %s | CurPos: %s"), 
					CurrentPhysicsStep, 
					*State.Position.ToString(), 
					*State.LinearVelocity.ToString(),
					*ParticleHandle->GetX().ToString());
			}

			ParticleHandle->SetX(State.Position);
			ParticleHandle->SetR(State.Rotation);
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
		if (!bIsResimming)
		{
			CurrentPhysicsStep = PhysicsStep;

			const FAsyncInputShip* AsyncInput = GetConsumerInput_Internal();
			if (CurrentPhysicsStep % 60 == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] ProcessInputs_Internal - Step: %d | AsyncInput: %s"), 
					CurrentPhysicsStep, AsyncInput ? TEXT("Valid") : TEXT("Null"));
			}

			if (AsyncInput)
			{
				MovementInput_Internal = AsyncInput->MovementInput;
				SteeringInput_Internal = AsyncInput->SteeringInput;

				// 최초 마샬링 시에만 필요한 폰툰 및 파도 설정 캐싱
				if (AsyncInput->PontoonOffsets.Num() > 0)
				{
					CachedPontoonOffsets = AsyncInput->PontoonOffsets;
				}
				if (AsyncInput->GerstnerWaves.Num() > 0)
				{
					CachedGerstnerWaves = AsyncInput->GerstnerWaves;
				}
				if (AsyncInput->SpawnWorldTime >= 0.0f)
				{
					CachedSpawnWorldTime = AsyncInput->SpawnWorldTime;
					if (CachedSpawnPhysicsStep < 0)
					{
						CachedSpawnPhysicsStep = CurrentPhysicsStep;
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
			}
		}
	}

	// 안전장치: PhysicsObject 유효성 검사 최우선 배치로 레이스 컨디션 차단
	if (!PhysicsObject)
	{
		if (CurrentPhysicsStep % 60 == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] ProcessInputs_Internal - Step: %d | PhysicsObject: Null (Skip Simulation)"), CurrentPhysicsStep);
		}
		return;
	}

	Chaos::FWritePhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetWrite();
	Chaos::FPBDRigidParticleHandle* ParticleHandle = Interface.GetRigidParticle(PhysicsObject);
	if (!ParticleHandle)
	{
		if (CurrentPhysicsStep % 60 == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] ProcessInputs_Internal - Step: %d | ParticleHandle is NULL!"), CurrentPhysicsStep);
		}
		return;
	}
	if (ParticleHandle->Disabled())
	{
		if (CurrentPhysicsStep % 60 == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] ProcessInputs_Internal - Step: %d | ParticleHandle is DISABLED!"), CurrentPhysicsStep);
		}
		return;
	}

	// 60틱 주기 정밀 상태 모니터링 로그
	if (CurrentPhysicsStep % 60 == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] ProcessInputs_Internal - Step: %d | PhysicsObject: Valid | IsSleeping: %s | IsResimming: %s"), 
			CurrentPhysicsStep, ParticleHandle->Sleeping() ? TEXT("True") : TEXT("False"), bIsResimming ? TEXT("True") : TEXT("False"));
	}

	// 잠자기 방지 설정
	ParticleHandle->SetSleepType(Chaos::ESleepType::NeverSleep);

	// 1. 결정론적 타임스탬프 계산 (Spawn 절대 월드 시간 오프셋 적용하여 파고 동기화 정밀도 보장)
	float SimTime = 0.0f;
	if (CachedSpawnWorldTime >= 0.0f && CachedSpawnPhysicsStep >= 0)
	{
		SimTime = CachedSpawnWorldTime + (CurrentPhysicsStep - CachedSpawnPhysicsStep) * 0.0166667f;
	}
	else
	{
		SimTime = CurrentPhysicsStep * 0.0166667f;
	}

	FVector ActorLocation = ParticleHandle->GetX();
	FQuat ActorRotation = ParticleHandle->GetR();

	if (bIsResimming)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] RESIMULATE TICK - Step: %d | SimTime: %.4f | Loc: %s"), 
			CurrentPhysicsStep, SimTime, *ActorLocation.ToString());
	}

	// 2. 가로축 수력 드래그 (Lateral Hydrodynamic Drag) 계산 및 적용
	if (CachedLateralDrag > 0.0f)
	{
		FVector Velocity = ParticleHandle->GetV();
		FVector Right = ActorRotation.GetRightVector();
		Right.Z = 0.0f;
		Right.Normalize();

		float LateralSpeed = FVector::DotProduct(Velocity, Right);
		FVector LateralDragForce = -Right * LateralSpeed * CachedLateralDrag;
		
		ParticleHandle->AddForce(LateralDragForce);
	}

	// 3. 커스텀 폰툰 기반 부력 연산 (Archimedes Buoyancy & Damping)
	if (CachedPontoonOffsets.Num() > 0 && CachedGerstnerWaves.Num() > 0)
	{
		FVector TotalBuoyancyForce = FVector::ZeroVector;
		FVector TotalBuoyancyTorque = FVector::ZeroVector;
		
		float GravityMag = FMath::Abs(CachedGravityZ);
		float InvMass = ParticleHandle->InvM();
		float Mass = (InvMass > 0.0f) ? (1.0f / InvMass) : 1000.f;
		
		// 각 폰툰당 부담해야 할 기본 중력 대응 힘
		float ForcePerPontoon = (Mass * GravityMag) / CachedPontoonOffsets.Num();
		
		for (const FVector& Offset : CachedPontoonOffsets)
		{
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

			if (Submersion > 0.0f)
			{
				// 구체 폰툰 체적 적분 계산 (Spherical Cap Volume)
				float SubDiff = FMath::Clamp(Submersion, 0.f, 2.f * CachedBuoyancyRadius);
				float SubDiffSq = SubDiff * SubDiff;
				float SubVolume = (PI / 3.f) * SubDiffSq * ((3.f * CachedBuoyancyRadius) - SubDiff);

				// 폰툰 로컬 속도 추출
				FVector PontoonVelocity = ParticleHandle->GetV() + FVector::CrossProduct(ParticleHandle->GetW(), PontoonWorldPos - ActorLocation);
				float VelocityZ = PontoonVelocity.Z;

				// 1차 및 2차 감쇄력 연산
				float FirstOrderDrag = CachedWaterDamping * VelocityZ;
				float SecondOrderDrag = FMath::Sign(VelocityZ) * CachedWaterDamping2 * VelocityZ * VelocityZ;
				float DampingFactorZ = -(FirstOrderDrag + SecondOrderDrag);

				// 부력 합산 및 최하단 클램핑 (음수 힘 방지)
				float PontoonForceZ = SubVolume * CachedBuoyancyForceMultiplier + DampingFactorZ;
				PontoonForceZ = FMath::Max(PontoonForceZ, 0.f);

				FVector PontoonTotalForce = FVector::UpVector * PontoonForceZ;

				TotalBuoyancyForce += PontoonTotalForce;
				TotalBuoyancyTorque += FVector::CrossProduct(PontoonWorldPos - ActorLocation, PontoonTotalForce);
			}
		}

		ParticleHandle->AddForce(TotalBuoyancyForce);
		ParticleHandle->AddTorque(TotalBuoyancyTorque);

		if (CurrentPhysicsStep % 60 == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] Buoyancy Applied - Step: %d | ForceZ: %.2f | Torq: %s | Pontoons: %d | Waves: %d"),
				CurrentPhysicsStep, TotalBuoyancyForce.Z, *TotalBuoyancyTorque.ToString(), CachedPontoonOffsets.Num(), CachedGerstnerWaves.Num());
		}
	}
	else
	{
		if (CurrentPhysicsStep % 60 == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] Buoyancy Skipped - Step: %d | Pontoons: %d | Waves: %d"),
				CurrentPhysicsStep, CachedPontoonOffsets.Num(), CachedGerstnerWaves.Num());
		}
	}

	// 4. 명시적 중력(Gravity) 적용 (물리 스레드 내 엔진 중력 증발 현상 대응)
	{
		float InvMass = ParticleHandle->InvM();
		float Mass = (InvMass > 0.0f) ? (1.0f / InvMass) : 1000.f;
		FVector GravityForce = FVector(0.f, 0.f, CachedGravityZ) * Mass;
		ParticleHandle->AddForce(GravityForce);

		if (CurrentPhysicsStep % 60 == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT-GRAVITY] Step: %d | InvMass: %.6f | Mass: %.2f | GravityZ: %.2f | GravityForceZ: %.2f"),
				CurrentPhysicsStep, InvMass, Mass, CachedGravityZ, GravityForce.Z);
		}
	}

	// 4. WASD 조작 물리 추진력 적용
	if (FMath::Abs(MovementInput_Internal) > KINDA_SMALL_NUMBER || FMath::Abs(SteeringInput_Internal) > KINDA_SMALL_NUMBER)
	{
		// 전진 힘
		if (FMath::Abs(MovementInput_Internal) > KINDA_SMALL_NUMBER)
		{
			FVector Forward = ActorRotation.GetForwardVector();
			Forward.Z = 0.0f;
			Forward.Normalize();

			ParticleHandle->AddForce(Forward * CachedForwardForce * MovementInput_Internal * CachedSpeedMultiplier);
		}

		// 회전 토크
		if (FMath::Abs(SteeringInput_Internal) > KINDA_SMALL_NUMBER)
		{
			ParticleHandle->AddTorque(FVector(0.f, 0.f, CachedTurnTorque * SteeringInput_Internal * CachedSpeedMultiplier));
		}
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
