#pragma once

#include "Chaos/SimCallbackObject.h"
#include "Chaos/PhysicsObject.h"
#include "Chaos/PhysicsObjectInterface.h"
#include "Chaos/PhysicsObjectInternalInterface.h"
#include "Chaos/Framework/PhysicsSolverBase.h"
#include "PBDRigidsSolver.h"

/** Read-only observer. Never adds forces or changes particle state. */
struct FChestLaunchPhysicsData
{
	Chaos::FConstPhysicsObjectHandle Object = nullptr;
	uint32 Sequence = 0;
	uint32 Request = 0;
	double WorldTime = 0.0;
	double ServerTime = 0.0;
	float ForceZ = 0.0f;
	float GravityZ = 0.0f;
	float WaterHeight = 0.0f;
	float WaveReferenceTime = 0.0f;
	float EffectiveWaveTime = 0.0f;
};

struct FChestLaunchPhysicsInput : Chaos::FSimCallbackInput, FChestLaunchPhysicsData
{
	void Reset() { static_cast<FChestLaunchPhysicsData&>(*this) = FChestLaunchPhysicsData(); }
};

struct FChestLaunchPhysicsSample
{
	int32 Frame = 0;
	double Time = 0.0;
	float Dt = 0.0f;
	FChestLaunchPhysicsData Input;
	FVector Position = FVector::ZeroVector;
	FVector BeforeVelocity = FVector::ZeroVector;
	FVector IntegratedVelocity = FVector::ZeroVector;
	FVector AfterVelocity = FVector::ZeroVector;
	FVector Acceleration = FVector::ZeroVector;
	float Mass = 0.0f;
};

struct FChestLaunchPhysicsOutput : Chaos::FSimCallbackOutput
{
	FChestLaunchPhysicsSample Samples[64];
	int32 Count = 0;
	uint32 Request = 0;
	double TriggerTime = 0.0;
	bool bPhysicsTrigger = false;
	void Reset() { Count = 0; Request = 0; TriggerTime = 0.0; bPhysicsTrigger = false; }
};

class FChestLaunchPhysicsDiagnostic : public Chaos::TSimCallbackObject<
	FChestLaunchPhysicsInput, FChestLaunchPhysicsOutput,
	Chaos::ESimCallbackOptions::Presimulate | Chaos::ESimCallbackOptions::PreIntegrate
	| Chaos::ESimCallbackOptions::PostIntegrate | Chaos::ESimCallbackOptions::PostSolve
	| Chaos::ESimCallbackOptions::PhysicsObjectUnregister>
{
public:
	virtual void OnPreSimulate_Internal() override
	{
		if (const FChestLaunchPhysicsInput* Input = GetConsumerInput_Internal(); Input && Input->Sequence != LatestInput.Sequence)
		{
			LatestInput = *Input;
			Object = Input->Object;
		}
	}
	virtual void OnPhysicsObjectUnregistered_Internal(Chaos::FConstPhysicsObjectHandle InObject) override
	{
		if (Object == InObject) { Object = nullptr; LatestInput.Object = nullptr; }
	}
	virtual void OnPreIntegrate_Internal() override
	{
		bSampleValid = false;
		if (!Object) return;
		auto Interface = Chaos::FPhysicsObjectInternalInterface::GetRead();
		if (const Chaos::FPBDRigidParticleHandle* Particle = Interface.GetRigidParticle(Object))
		{
			Current = FChestLaunchPhysicsSample();
			Current.Frame = static_cast<Chaos::FPBDRigidsSolver*>(GetSolver())->GetCurrentFrame();
			Current.Time = GetSimTime_Internal();
			Current.Dt = GetDeltaTime_Internal();
			Current.Input = LatestInput;
			Current.Position = Particle->GetX();
			Current.BeforeVelocity = Particle->GetV();
			Current.Acceleration = Particle->Acceleration();
			Current.Mass = Particle->M();
			bSampleValid = true;
		}
	}
	virtual void OnPostIntegrate_Internal() override
	{
		if (!bSampleValid || !Object) return;
		auto Interface = Chaos::FPhysicsObjectInternalInterface::GetRead();
		if (const Chaos::FPBDRigidParticleHandle* Particle = Interface.GetRigidParticle(Object))
		{
			Current.IntegratedVelocity = Particle->GetV();
		}
	}
	virtual void OnPostSolve_Internal() override
	{
		if (!bSampleValid || !Object) return;
		auto Interface = Chaos::FPhysicsObjectInternalInterface::GetRead();
		const Chaos::FPBDRigidParticleHandle* Particle = Interface.GetRigidParticle(Object);
		if (!Particle) return;
		Current.AfterVelocity = Particle->GetV();
		History[Next] = Current;
		Next = (Next + 1) % 64;
		Count = FMath::Min(Count + 1, 64);
		const bool bPhysicsAnomaly = Current.AfterVelocity.Z - Current.BeforeVelocity.Z > 150.0f
			|| (Current.AfterVelocity.Z > 400.0f && Current.BeforeVelocity.Z <= 400.0f);
		const bool bRequested = LatestInput.Request != LastRequest;
		LastRequest = LatestInput.Request;
		if (!bPending && Current.Time >= NextCaptureTime && (bRequested || bPhysicsAnomaly))
		{
			bPending = true;
			TriggerTime = Current.Time;
			CaptureRequest = LatestInput.Request;
			bPhysicsTrigger = bPhysicsAnomaly;
			NextCaptureTime = Current.Time + 10.0;
		}
		// One bounded dump includes the lead-in and 0.3 seconds after detection.
		if (bPending && Current.Time >= TriggerTime + 0.3)
		{
			FChestLaunchPhysicsOutput& Output = GetProducerOutputData_Internal();
			Output.Count = Count;
			Output.Request = CaptureRequest;
			Output.TriggerTime = TriggerTime;
			Output.bPhysicsTrigger = bPhysicsTrigger;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Output.Samples[Index] = History[(Next - Count + Index + 64) % 64];
			}
			bPending = false;
		}
	}
private:
	Chaos::FConstPhysicsObjectHandle Object = nullptr;
	FChestLaunchPhysicsData LatestInput;
	FChestLaunchPhysicsSample Current;
	FChestLaunchPhysicsSample History[64];
	int32 Next = 0;
	int32 Count = 0;
	uint32 LastRequest = 0;
	uint32 CaptureRequest = 0;
	bool bSampleValid = false;
	bool bPending = false;
	bool bPhysicsTrigger = false;
	double TriggerTime = 0.0;
	double NextCaptureTime = 0.0;
};
