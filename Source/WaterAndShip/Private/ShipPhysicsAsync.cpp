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
	if (PhysicsObject)
	{
		Chaos::FWritePhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetWrite();
		if (Chaos::FPBDRigidParticleHandle* ParticleHandle = Interface.GetRigidParticle(PhysicsObject))
		{
			// 잠듦 방지 적용
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
}

void FShipPhysicsAsync::ApplyInput_Internal(const FNetInputShip& Input)
{
	MovementInput_Internal = Input.MovementInput;
	SteeringInput_Internal = Input.SteeringInput;
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
			ParticleHandle->SetV(State.LinearVelocity);
			ParticleHandle->SetW(State.AngularVelocity);
		}
	}
}

void FShipPhysicsAsync::ProcessInputs_Internal(int32 PhysicsStep)
{
	if (Chaos::FPhysicsSolverBase* CurrentSolver = GetSolver())
	{
		if (!CurrentSolver->IsResimming())
		{
			CurrentPhysicsStep = PhysicsStep;

			if (const FAsyncInputShip* AsyncInput = GetConsumerInput_Internal())
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
					CachedSpawnPhysicsStep = AsyncInput->SpawnPhysicsStep;
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
}

void FShipPhysicsAsync::OnPreSimulate_Internal()
{
	// 안전장치: PhysicsObject 유효성 검사 최우선 배치로 레이스 컨디션 차단
	if (!PhysicsObject)
	{
		return;
	}

	Chaos::FWritePhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetWrite();
	Chaos::FPBDRigidParticleHandle* ParticleHandle = Interface.GetRigidParticle(PhysicsObject);
	if (!ParticleHandle || ParticleHandle->Disabled())
	{
		return;
	}

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

	if (Chaos::FPhysicsSolverBase* CurrentSolver = GetSolver())
	{
		if (CurrentSolver->IsResimming())
		{
			UE_LOG(LogTemp, Warning, TEXT("[PHYSICS-PT] RESIMULATE TICK - Step: %d | SimTime: %.4f | Loc: %s"), 
				CurrentPhysicsStep, SimTime, *ActorLocation.ToString());
		}
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
			if (Depth > 0.0f)
			{
				// 구체 폰툰의 반경 기준으로 물에 잠긴 볼륨 비율 산출
				float Submersion = FMath::Clamp((Depth + CachedBuoyancyRadius) / (2.0f * CachedBuoyancyRadius), 0.0f, 1.0f);
				
				// 부력 힘 = 기본 대응 힘 * 잠긴 비율 * 보정 배율
				FVector BuoyantForce = FVector::UpVector * ForcePerPontoon * Submersion * CachedBuoyancyForceMultiplier;
				
				// 물 댐핑 감쇄력 계산 (속도 상쇄)
				FVector PontoonVelocity = ParticleHandle->GetV() + FVector::CrossProduct(ParticleHandle->GetW(), PontoonWorldPos - ActorLocation);
				FVector DampingForce = -PontoonVelocity * CachedWaterDamping * Submersion * (ForcePerPontoon / GravityMag);
				
				FVector PontoonTotalForce = BuoyantForce + DampingForce;
				
				TotalBuoyancyForce += PontoonTotalForce;
				TotalBuoyancyTorque += FVector::CrossProduct(PontoonWorldPos - ActorLocation, PontoonTotalForce);
			}
		}

		ParticleHandle->AddForce(TotalBuoyancyForce);
		ParticleHandle->AddTorque(TotalBuoyancyTorque);
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

float FShipPhysicsAsync::GetWaveHeightAtPosition_Internal(const FVector& Position, float Time, const TArray<FGerstnerWave>& Waves, float Gravity) const
{
	FVector TotalDisplacement = FVector::ZeroVector;

	for (const FGerstnerWave& Wave : Waves)
	{
		float k = (2.0f * PI) / Wave.WaveLength;
		float DirectionDotPosition = Wave.Direction.X * Position.X + Wave.Direction.Y * Position.Y;
		float Omega = FMath::Sqrt(Gravity * k);
		float Theta = k * DirectionDotPosition - Omega * Time;

		float CosTheta = FMath::Cos(Theta);
		float SinTheta = FMath::Sin(Theta);

		float Q = Wave.Steepness / (Wave.Amplitude * k * Waves.Num());

		TotalDisplacement.X += Q * Wave.Amplitude * CosTheta * Wave.Direction.X;
		TotalDisplacement.Y += Q * Wave.Amplitude * CosTheta * Wave.Direction.Y;
		TotalDisplacement.Z += Wave.Amplitude * SinTheta;
	}

	return TotalDisplacement.Z;
}
