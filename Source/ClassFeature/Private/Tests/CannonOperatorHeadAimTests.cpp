#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/CannonOperatorHeadAim.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCannonHeadBoundaryTest,
	"ArtisticSW.Cannon.Operator.HeadAim.RearContactBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCannonHeadBoundaryTest::RunTest(const FString&)
{
	const FVector F=FVector::ForwardVector, R=FVector::RightVector, U=FVector::UpVector;
	const auto PreviousLook=[&](const FVector& Grip)
	{
		const FQuat Q=FQuat(U,FMath::Atan2(Grip.Y,Grip.X))
			*FQuat(R,FMath::Atan2(-Grip.Z,FMath::Sqrt(Grip.X*Grip.X+Grip.Y*Grip.Y)));
		return FQuat::Slerp(FQuat::Identity,Q,.25f).GetNormalized();
	};
	const FVector Left(-.1,-.001,-1), Right(-.1,.001,-1);
	TestTrue(TEXT("The original rear/down Euler-quaternion blend reproduces a large flip"),
		FMath::RadiansToDegrees(PreviousLook(Left).AngularDistance(PreviousLook(Right)))>20.f);
	const FSWCannonHeadLimits Limits;
	for (const float Weight : {0.f,.25f,.5f,1.f})
	{
		const FQuat A=SWCannonHeadAim::Target(FQuat::Identity,F,Left,Weight,F,R,U,Limits);
		const FQuat B=SWCannonHeadAim::Target(FQuat::Identity,F,Right,Weight,F,R,U,Limits);
		TestTrue(TEXT("Crossing the rear-contact seam remains continuous"),
			FMath::RadiansToDegrees(A.AngularDistance(B))<3.f);
		const FQuat Opposite=SWCannonHeadAim::Target(FQuat::Identity,F,-F,Weight,F,R,U,Limits);
		TestFalse(TEXT("An opposite or zero direction never creates an invalid rotation"),Opposite.ContainsNaN());
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCannonHeadLimitsTest,
	"ArtisticSW.Cannon.Operator.HeadAim.ChestLimitsAndRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCannonHeadLimitsTest::RunTest(const FString&)
{
	const FVector F=FVector::ForwardVector, R=FVector::RightVector, U=FVector::UpVector;
	FSWCannonHeadLimits Limits;
	const FQuat Body=FRotator(-60,25,30).Quaternion();
	for (int32 Yaw=-180;Yaw<=180;Yaw+=15) for (int32 Pitch=-90;Pitch<=90;Pitch+=15)
	{
		const FVector Direction=Body.RotateVector(FRotator(Pitch,Yaw,0).Vector());
		const FQuat Q=SWCannonHeadAim::Target(Body,Direction,Direction,1.f,F,R,U,Limits);
		const FRotator Local=Q.RotateVector(F).Rotation();
		TestTrue(TEXT("Yaw limit is measured from the already leaned chest"),FMath::Abs(Local.Yaw)<=Limits.Yaw+.01f);
		TestTrue(TEXT("Neck pitch stays within its up/down limits"),Local.Pitch<=Limits.PitchUp+.01f && Local.Pitch>=-Limits.PitchDown-.01f);
		TestTrue(TEXT("The overall neck cone is bounded"),FMath::RadiansToDegrees(Q.GetAngle())<=Limits.Cone+.01f);
	}
	const FQuat Desired=SWCannonHeadAim::Target(FQuat::Identity,R,-U,.25f,F,R,U,Limits);
	for (const float Dt : {1.f/120.f,1.f/60.f,1.f/30.f})
	{
		FQuat Current=FQuat::Identity;
		for (int32 Frame=0;Frame<240;++Frame)
		{
			const FQuat Next=SWCannonHeadAim::Follow(Current,Desired,Dt,F,R,U,Limits);
			TestTrue(TEXT("No frame exceeds the configured neck turn rate"),
				FMath::RadiansToDegrees(Current.AngularDistance(Next))<=Limits.DegreesPerSecond*Dt+.01f);
			Current=Next;
		}
		TestTrue(TEXT("Tracking settles at the intended direction"),Current.AngularDistance(Desired)<.001f);
		TestTrue(TEXT("Repeated evaluation at the same timestamp cannot advance the filter"),
			SWCannonHeadAim::Follow(Current,FQuat::Identity,0.f,F,R,U,Limits).Equals(Current,.00001f));
	}
	FQuat Corner=FQuat(U,FMath::DegreesToRadians(-Limits.Yaw))*FQuat(R,FMath::DegreesToRadians(Limits.PitchDown));
	const FQuat OtherCorner=FQuat(U,FMath::DegreesToRadians(Limits.Yaw))*FQuat(R,FMath::DegreesToRadians(Limits.PitchDown));
	for(int32 Frame=0;Frame<120;++Frame)
	{
		Corner=SWCannonHeadAim::Follow(Corner,OtherCorner,1.f/30.f,F,R,U,Limits);
		TestTrue(TEXT("Tracking between limit corners cannot exceed the neck pitch limit"),
			Corner.RotateVector(F).Rotation().Pitch>=-Limits.PitchDown-.01f);
	}
	return !HasAnyErrors();
}
#endif
