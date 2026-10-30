#include "Animation/CannonOperatorSpineSolver.h"

FSWCannonSpinePose SWCannonSpine::Build(const FSWCannonSpineRig& R,const FSWCannonSpineState& S)
{
	FSWCannonSpinePose P;P.Hip=R.Hip;
	P.Hip.AddToTranslation(R.Forward*S.Offset.X+R.Right*S.Offset.Y+R.Up*S.Offset.Z);
	const float Weight[]={.45f,.30f,.15f,.07f,.03f};
	FQuat Curve=FQuat::Identity;
	FVector Parent= P.Hip.GetLocation(),RefParent=R.Hip.GetLocation();
	for(int32 I=0;I<5;++I)
	{
		const FQuat Delta=FQuat(R.Up,FMath::DegreesToRadians(S.Yaw*Weight[I]))
			*FQuat(R.Forward,FMath::DegreesToRadians(S.Side*Weight[I]))
			*FQuat(R.Right,FMath::DegreesToRadians(S.Bend*Weight[I]));
		Curve=(Delta*Curve).GetNormalized();P.Spine[I]=R.Spine[I];
		P.Spine[I].SetLocation(Parent+Curve.RotateVector(R.Spine[I].GetLocation()-RefParent));
		P.Spine[I].SetRotation((Curve*R.Spine[I].GetRotation()).GetNormalized());
		Parent=P.Spine[I].GetLocation();RefParent=R.Spine[I].GetLocation();
	}
	for(int32 I=0;I<2;++I)
	{
		P.Shoulder[I]=R.Shoulder[I].GetRelativeTransform(R.Spine[4])*P.Spine[4];
		P.Thigh[I]=R.Thigh[I]+P.Hip.GetLocation()-R.Hip.GetLocation();
	}
	return P;
}

FSWCannonSpineState SWCannonSpine::Solve(const FSWCannonSpineRig& R,const FSWCannonSpineLimits& L,const FSWCannonSpineState& Seed)
{
	const FVector Grip=(R.Goals[2]+R.Goals[3])*.5f;
	const auto Clamp=[&](FSWCannonSpineState S)
	{
		S.Offset.X=FMath::Clamp(S.Offset.X,0.f,L.ForwardShift);
		S.Offset.Y=FMath::Clamp(S.Offset.Y,-L.SideShift,L.SideShift);
		S.Offset.Z=FMath::Clamp(S.Offset.Z,-L.Crouch,0.f);
		S.Bend=FMath::Clamp(S.Bend,15.f,L.MaxBend);
		S.Side=FMath::Clamp(S.Side,-L.MaxSide,L.MaxSide);
		S.Yaw=FMath::Clamp(S.Yaw,-L.MaxYaw,L.MaxYaw);return S;
	};
	const auto Cost=[&](const FSWCannonSpineState& S)
	{
		const FSWCannonSpinePose P=Build(R,S);
		float Value=0.f;
		// A candidate that would pull either planted foot is never accepted.
		for(int32 I=0;I<2;++I)
		{
			const float Over=FVector::Distance(P.Thigh[I],R.Goals[I])-R.Reach[I]*.98f;
			if(Over>.001f)return 1.e7f+Over*Over*1.e4f;
		}
		for(int32 I=0;I<2;++I)
		{
			const float Over=FMath::Max(0.f,FVector::Distance(P.Shoulder[I].GetLocation(),R.Goals[I+2])-R.Reach[I+2]*.96f);
			Value+=Over*Over*40.f;
		}
		const FVector Desired=Grip+R.Up*L.ShoulderAboveGrip;
		Value+=FVector::DistSquared((P.Shoulder[0].GetLocation()+P.Shoulder[1].GetLocation())*.5f,Desired)*.08f;
		Value+=S.Offset.SizeSquared()*.18f;
		const FVector D=Desired-P.Hip.GetLocation();
		const float F=FVector::DotProduct(D,R.Forward),Y=FVector::DotProduct(D,R.Right),U=FVector::DotProduct(D,R.Up);
		const float PreferredBend=FMath::Clamp((FMath::RadiansToDegrees(FMath::Atan2(F,U))+L.RestBend)*.5f,15.f,L.MaxBend);
		const float PreferredSide=FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(-Y,FMath::Sqrt(F*F+U*U))),-L.MaxSide,L.MaxSide);
		const float PreferredYaw=FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Y,F))*L.YawShare,-L.MaxYaw,L.MaxYaw);
		Value+=FMath::Square(S.Bend-PreferredBend)*.06f+FMath::Square(S.Side-PreferredSide)*.12f+FMath::Square(S.Yaw-PreferredYaw)*.1f;
		return Value;
	};
	FSWCannonSpineState Best=Clamp(Seed);float BestCost=Cost(Best);
	for(const float Step:{8.f,4.f,2.f,1.f})for(int32 Axis=0;Axis<6;++Axis)
	{
		FSWCannonSpineState Winner=Best;float WinnerCost=BestCost;
		for(const float Sign:{-1.f,1.f})
		{
			FSWCannonSpineState Candidate=Best;
			if(Axis<3)Candidate.Offset[Axis]+=Step*Sign;
			else if(Axis==3)Candidate.Bend+=Step*Sign;
			else if(Axis==4)Candidate.Side+=Step*Sign;
			else Candidate.Yaw+=Step*Sign;
			Candidate=Clamp(Candidate);const float V=Cost(Candidate);
			if(V<WinnerCost){Winner=Candidate;WinnerCost=V;}
		}
		Best=Winner;BestCost=WinnerCost;
	}
	return Best;
}
