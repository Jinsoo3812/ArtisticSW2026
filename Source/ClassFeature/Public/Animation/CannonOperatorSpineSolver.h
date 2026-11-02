#pragma once
#include "CoreMinimal.h"

/** Mesh-space, actor-independent reach solve. Feet are hard constraints. */
struct FSWCannonSpineState
{
	FVector Offset=FVector::ZeroVector;
	float Bend=55.f, Side=0.f, Yaw=0.f;
};
struct FSWCannonSpineRig
{
	FTransform Hip, Spine[5], Shoulder[2];
	FVector Thigh[2], Goals[4], Forward, Right, Up;
	float Reach[4];
};
struct FSWCannonSpineLimits
{
	float ForwardShift=35.f, SideShift=30.f, Crouch=40.f;
	float MaxBend=70.f, MaxSide=45.f, MaxYaw=25.f, YawShare=.35f;
	float RestBend=55.f, ShoulderAboveGrip=48.f;
};
struct FSWCannonSpinePose
{
	FTransform Hip, Spine[5], Shoulder[2];
	FVector Thigh[2];
};
namespace SWCannonSpine
{
	CLASSFEATURE_API FSWCannonSpinePose Build(const FSWCannonSpineRig& Rig,const FSWCannonSpineState& State);
	CLASSFEATURE_API FSWCannonSpineState Solve(const FSWCannonSpineRig& Rig,const FSWCannonSpineLimits& Limits,const FSWCannonSpineState& Seed);
}
