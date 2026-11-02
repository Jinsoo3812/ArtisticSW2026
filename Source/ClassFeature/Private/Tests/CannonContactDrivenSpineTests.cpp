#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/CannonOperatorSpineSolver.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCannonContactDrivenSpineTest,"ArtisticSW.Cannon.Operator.ContactDrivenSpine",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCannonContactDrivenSpineTest::RunTest(const FString&)
{
 FSWCannonSpineRig R;R.Forward=FVector::ForwardVector;R.Right=FVector::RightVector;R.Up=FVector::UpVector;
 R.Hip=FTransform(FVector(0,0,80));for(int32 I=0;I<5;++I)R.Spine[I]=FTransform(FVector(0,0,90+10*I));
 R.Shoulder[0]=FTransform(FVector(0,-18,130));R.Shoulder[1]=FTransform(FVector(0,18,130));
 R.Thigh[0]=FVector(0,-10,75);R.Thigh[1]=FVector(0,10,75);
 R.Goals[0]=FVector(0,-12,12);R.Goals[1]=FVector(0,12,12);
 R.Reach[0]=R.Reach[1]=85;R.Reach[2]=R.Reach[3]=60;
 for(const float Side:{-1.f,1.f})
 {
  R.Goals[2]=FVector(50,Side*35,60);R.Goals[3]=FVector(50,Side*15,60);
  FSWCannonSpineState S;
  for(int32 I=0;I<10;++I)S=SWCannonSpine::Solve(R,FSWCannonSpineLimits(),S);
  const FSWCannonSpinePose P=SWCannonSpine::Build(R,S);
  TestTrue(TEXT("Spine bends toward the actual rear contact side"),(P.Spine[4].GetLocation().Y-P.Hip.GetLocation().Y)*Side>2.);
  TestTrue(TEXT("Lateral flexion has the anatomical sign for that contact side"),S.Side*Side<0.f);
  for(int32 I=0;I<2;++I)TestTrue(TEXT("Both planted feet remain reachable"),FVector::Distance(P.Thigh[I],R.Goals[I])<=R.Reach[I]*.98f+.01f);
  FVector Previous=P.Hip.GetLocation(),RefPrevious=R.Hip.GetLocation();
  for(int32 I=0;I<5;++I)
  {
   TestTrue(TEXT("Spine segments are not stretched"),FMath::Abs(FVector::Distance(P.Spine[I].GetLocation(),Previous)-FVector::Distance(R.Spine[I].GetLocation(),RefPrevious))<.01);
   Previous=P.Spine[I].GetLocation();RefPrevious=R.Spine[I].GetLocation();
  }
 }
 return !HasAnyErrors();
}
#endif
