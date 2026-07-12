#pragma once

#include "CoreMinimal.h"
#include "Chaos/SimCallbackObject.h"
#include "Chaos/PhysicsObject.h"
#include "Physics/NetworkPhysicsComponent.h"
#include "GerstnerWaterWaves.h"
#include "Ship.h"

// GameThread -> PhysicsThread 마샬링용 구조체
struct FAsyncInputShip : public Chaos::FSimCallbackInput
{
	float MovementInput = 0.0f;
	float SteeringInput = 0.0f;

	// 폰툰 오프셋 데이터
	TArray<FVector> PontoonOffsets;
	
	// 파도 수학적 파라미터 (Gerstner Waves 복사본)
	TArray<FGerstnerWave> GerstnerWaves;

	// 중력 가속도, 드래그 계수 등 물리 설정 값
	float GravityZ = -980.f;
	float LateralDrag = 0.5f;
	float ForwardForceValue = 500000.f;
	float TurnTorqueValue = 20000000.f;
	float SpeedMultiplier = 1.0f;

	// 부력 관련 상세 세팅 (폰툰 개수 당 부력 세기, 반경 등)
	float BuoyancyRadius = 100.f;
	float BuoyancyForceMultiplier = 1.2f;
	float WaterDamping = 3.0f;

	// 위상 동기화용 시간/틱 오프셋
	float SpawnWorldTime = -1.0f;
	int32 SpawnPhysicsStep = -1;

	void Reset()
	{
		MovementInput = 0.0f;
		SteeringInput = 0.0f;
		PontoonOffsets.Empty();
		GerstnerWaves.Empty();
		SpawnWorldTime = -1.0f;
		SpawnPhysicsStep = -1;
	}
};

struct FAsyncOutputShip : public Chaos::FSimCallbackOutput
{
	void Reset() {}
};

class FShipPhysicsAsync : public Chaos::TSimCallbackObject<FAsyncInputShip, FAsyncOutputShip,
	(Chaos::ESimCallbackOptions::Presimulate | Chaos::ESimCallbackOptions::PhysicsObjectUnregister | Chaos::ESimCallbackOptions::Rewind)>
	, public TNetworkPhysicsInputState_Internal<FNetInputShip, FNetStatePhysicsShip>
{
public:
	FShipPhysicsAsync();
	virtual ~FShipPhysicsAsync() override;

	// TSimCallbackObject 인터페이스 구현
	virtual void OnPostInitialize_Internal() override;
	virtual void OnPreSimulate_Internal() override;
	virtual void ProcessInputs_Internal(int32 PhysicsStep) override;
	virtual void OnPhysicsObjectUnregistered_Internal(Chaos::FConstPhysicsObjectHandle InPhysicsObject) override;

	// TNetworkPhysicsInputState_Internal 인터페이스 구현
	virtual void BuildInput_Internal(FNetInputShip& Input) const override;
	virtual void ApplyInput_Internal(const FNetInputShip& Input) override;
	virtual void ValidateInput_Internal(FNetInputShip& Input) const override;
	virtual void BuildState_Internal(FNetStatePhysicsShip& State) const override;
	virtual void ApplyState_Internal(const FNetStatePhysicsShip& State) override;

	void SetPhysicsObject(Chaos::FConstPhysicsObjectHandle InObject) { PhysicsObject = InObject; }

private:
	// 물리 스레드 내부에서 계산할 실시간 파고 쿼리 함수 (순수 수학 연산, 100% 스레드 세이프)
	float GetWaveHeightAtPosition_Internal(const FVector& Position, float Time, const TArray<FGerstnerWave>& Waves, float Gravity) const;

	Chaos::FConstPhysicsObjectHandle PhysicsObject = nullptr;

	// 비동기 물리 틱 타임스탬프 계산용 스텝 카운터
	int32 CurrentPhysicsStep = 0;

	// 비동기 스레드 내부 입력 캐시
	float MovementInput_Internal = 0.0f;
	float SteeringInput_Internal = 0.0f;

	// 물리 스레드에서 고정 보관할 데이터들 (최초 전송 시 캐싱)
	TArray<FVector> CachedPontoonOffsets;
	TArray<FGerstnerWave> CachedGerstnerWaves;
	
	float CachedGravityZ = -980.f;
	float CachedLateralDrag = 0.5f;
	float CachedForwardForce = 500000.f;
	float CachedTurnTorque = 20000000.f;
	float CachedSpeedMultiplier = 1.0f;
	float CachedBuoyancyRadius = 100.f;
	float CachedBuoyancyForceMultiplier = 1.2f;
	float CachedWaterDamping = 3.0f;

	// 위상 동기화용 타임스탬프 캐시
	float CachedSpawnWorldTime = -1.0f;
	int32 CachedSpawnPhysicsStep = -1;
};
