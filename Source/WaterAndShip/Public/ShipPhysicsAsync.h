#pragma once

#include "CoreMinimal.h"
#include "Chaos/SimCallbackObject.h"
#include "Chaos/PhysicsObject.h"
#include "Physics/NetworkPhysicsComponent.h"
#include "GerstnerWaterWaves.h"
#include "Water/SWRippleTypes.h"
#include "Ship.h"

struct FAsyncInputShip : public Chaos::FSimCallbackInput
{
	float MovementInput = 0.0f;
	float SteeringInput = 0.0f;
	FVector ExternalAcceleration = FVector::ZeroVector;
	bool bHasLocalController = false;
	/** Server-authored gameplay force (vortex, knockback, etc.) may affect AI ships too. */
	bool bApplyAuthoritativeExternalAcceleration = false;
	/** Only authority writes the game-thread buoyancy state into Network Physics history. */
	bool bApplyAuthoritativeBuoyancyState = false;
	bool bBuoyancyEnabled = true;
	bool bQueryDiagnostics = false;

	TArray<FVector> PontoonOffsets;
	TArray<float> PontoonRadii;
	TArray<float> PontoonForceScales;
	TArray<FGerstnerWave> GerstnerWaves;
	TArray<FSWRippleEvent> RippleEvents;

	float GravityZ = -980.f;
	float LateralDrag = 0.5f;
	float ForwardForceValue = 2000000.f;
	float TurnTorqueValue = 6000000000.f;
	float ForwardPropulsionMultiplier = 1.0f;
	float TurnTorqueMultiplier = 1.0f;

	float BuoyancyRadius = 100.f;
	float BuoyancyForceMultiplier = 1.2f;
	float WaterDamping = 3.0f;
	float WaterDamping2 = 0.1f;
	float MaxBuoyantForce = 5000000.0f;

	double ServerPhysicsTimeOrigin = -1.0;
	float ServerPhysicsStepSeconds = 0.0f;
	int32 NetworkPhysicsTickOffset = 0;
	bool bNetworkPhysicsTickOffsetAssigned = false;

	float ResimLocationThreshold = 5.0f;
	float ResimRotationThreshold = 5.0f;

	void Reset()
	{
		MovementInput = 0.0f;
		SteeringInput = 0.0f;
		ExternalAcceleration = FVector::ZeroVector;
		bHasLocalController = false;
		bApplyAuthoritativeExternalAcceleration = false;
		bApplyAuthoritativeBuoyancyState = false;
		bBuoyancyEnabled = true;
		bQueryDiagnostics = false;
		PontoonOffsets.Empty();
		PontoonRadii.Empty();
		PontoonForceScales.Empty();
		GerstnerWaves.Empty();
		RippleEvents.Empty();
		ServerPhysicsTimeOrigin = -1.0;
		ServerPhysicsStepSeconds = 0.0f;
		NetworkPhysicsTickOffset = 0;
		bNetworkPhysicsTickOffsetAssigned = false;
		ResimLocationThreshold = 5.0f;
		ResimRotationThreshold = 5.0f;
	}
};

struct FAsyncOutputShip : public Chaos::FSimCallbackOutput
{
	bool bWaveSampleValid = false;
	bool bWasResimming = false;
	FVector WaveSamplePosition = FVector::ZeroVector;
	double WaveSampleServerTime = 0.0;
	float PTWaveHeight = 0.0f;

	void Reset()
	{
		bWaveSampleValid = false;
		bWasResimming = false;
		WaveSamplePosition = FVector::ZeroVector;
		WaveSampleServerTime = 0.0;
		PTWaveHeight = 0.0f;
	}
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
	FVector ExternalAcceleration_Internal = FVector::ZeroVector;
	bool bBuoyancyEnabled_Internal = true;
	bool bAuthoritativeBuoyancyWriter_Internal = false;
	bool bQueryDiagnostics_Internal = false;

	// 물리 스레드에서 고정 보관할 데이터들 (최초 전송 시 캐싱)
	TArray<FVector> CachedPontoonOffsets;
	TArray<float> CachedPontoonRadii;
	TArray<float> CachedPontoonForceScales;
	TArray<FGerstnerWave> CachedGerstnerWaves;
	TArray<FSWRippleEvent> CachedRippleEvents;
	
	float CachedGravityZ = -980.f;
	float CachedLateralDrag = 0.5f;
	float CachedForwardForce = 2000000.f;
	float CachedTurnTorque = 6000000000.f;
	float CachedForwardPropulsionMultiplier = 1.0f;
	float CachedTurnTorqueMultiplier = 1.0f;
	float CachedBuoyancyRadius = 100.f;
	float CachedBuoyancyForceMultiplier = 1.2f;
	float CachedWaterDamping = 3.0f;
	float CachedWaterDamping2 = 0.1f;
	float CachedMaxBuoyantForce = 5000000.0f;

	// Authoritative server-frame clock used in normal simulation and rewind.
	double CachedServerPhysicsTimeOrigin = -1.0;
	float CachedServerPhysicsStepSeconds = 0.0f;
	int32 CachedNetworkPhysicsTickOffset = 0;
	bool bHasNetworkPhysicsTickOffset = false;

	// 에디터 연동 롤백 오차 임계값 캐시
	float CachedResimLocationThreshold = 5.f;
	float CachedResimRotationThreshold = 5.f;
};
