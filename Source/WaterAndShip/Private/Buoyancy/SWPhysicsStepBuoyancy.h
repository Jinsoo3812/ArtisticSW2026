#pragma once

#include "Chaos/SimCallbackObject.h"
#include "Chaos/PhysicsObject.h"
#include "Chaos/PhysicsObjectInternalInterface.h"
#include "Water/SWBuoyancyMath.h"

/** GT water query result, with no UObject access required by the physics solver. */
struct FSWPhysicsBuoyancyPontoon
{
	FVector ScaledLocalOffset = FVector::ZeroVector;
	FVector WaterVelocity = FVector::ZeroVector;
	float WaterHeight = -BIG_NUMBER;
	float Radius = 50.0f;
	float ForceScale = 1.0f;

	FSWBuoyancySolveResult Solve(const FVector& WorldPosition, const FVector& PointVelocity,
		const FSWBuoyancyForceSettings& Settings) const
	{
		FSWBuoyancySolveInput Input;
		Input.WaterHeight = WaterHeight;
		Input.PontoonCenterZ = WorldPosition.Z;
		Input.PontoonRadius = Radius;
		Input.RelativeVelocityZ = PointVelocity.Z - WaterVelocity.Z;
		Input.ForceScale = ForceScale;
		return FSWBuoyancyMath::SolvePontoon(Input, Settings);
	}
};

struct FSWPhysicsStepBuoyancyInput : Chaos::FSimCallbackInput
{
	Chaos::FPhysicsObjectHandle Object = nullptr;
	uint32 Sequence = 0;
	TArray<FSWPhysicsBuoyancyPontoon> Pontoons;
	FSWBuoyancyForceSettings Settings;
	void Reset()
	{
		Object = nullptr;
		Sequence = 0;
		Pontoons.Reset();
		Settings = FSWBuoyancyForceSettings();
	}
};

/** Authority-only force writer. Recompute feedback for every actual Chaos step. */
class FSWPhysicsStepBuoyancy : public Chaos::TSimCallbackObject<
	FSWPhysicsStepBuoyancyInput, Chaos::FSimCallbackNoOutput,
	Chaos::ESimCallbackOptions::Presimulate | Chaos::ESimCallbackOptions::PhysicsObjectUnregister>
{
public:
	virtual void OnPreSimulate_Internal() override
	{
		if (const auto* Input = GetConsumerInput_Internal(); Input && Input->Sequence != Sequence)
		{
			Sequence = Input->Sequence;
			Object = Input->Object;
			Pontoons = Input->Pontoons;
			Settings = Input->Settings;
		}
		if (!Object) return;
		auto Interface = Chaos::FPhysicsObjectInternalInterface::GetWrite();
		Chaos::FPBDRigidParticleHandle* Particle = Interface.GetRigidParticle(Object);
		if (!Particle || Particle->Disabled() || Particle->InvM() <= 0.0) return;

		const FVector Position = Particle->GetX();
		const FQuat Rotation = Particle->GetR();
		const FVector CenterOfMass = Position + Rotation.RotateVector(Particle->CenterOfMass());
		FVector TotalForce = FVector::ZeroVector;
		FVector TotalTorque = FVector::ZeroVector;
		for (const FSWPhysicsBuoyancyPontoon& Pontoon : Pontoons)
		{
			const FVector WorldPosition = Position + Rotation.RotateVector(Pontoon.ScaledLocalOffset);
			const FVector LeverArm = WorldPosition - CenterOfMass;
			const FVector PointVelocity = Particle->GetV() + FVector::CrossProduct(Particle->GetW(), LeverArm);
			const auto Result = Pontoon.Solve(WorldPosition, PointVelocity, Settings);
			const FVector Force = FVector::UpVector * Result.BuoyantForceZ;
			TotalForce += Force;
			TotalTorque += FVector::CrossProduct(LeverArm, Force);
		}
		if (!TotalForce.IsNearlyZero())
		{
			// Match the existing GT AddForceAtLocation wake behavior for floating bodies.
			Interface.WakeUp(MakeArrayView(&Object, 1));
			Particle->AddForce(TotalForce);
			Particle->AddTorque(TotalTorque);
		}
	}

	virtual void OnPhysicsObjectUnregistered_Internal(Chaos::FConstPhysicsObjectHandle InObject) override
	{
		if (Object == InObject) Object = nullptr;
	}

private:
	Chaos::FPhysicsObjectHandle Object = nullptr;
	uint32 Sequence = 0;
	TArray<FSWPhysicsBuoyancyPontoon> Pontoons;
	FSWBuoyancyForceSettings Settings;
};
