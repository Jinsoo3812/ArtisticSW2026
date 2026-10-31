#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimNode_SWCannonOperator.h"
#include "Animation/AnimNode_Root.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCannonAnimGraphLoadTest,
 "ArtisticSW.Cannon.Operator.UncookedGameGraphLoads",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCannonAnimGraphLoadTest::RunTest(const FString& Parameters)
{
 for (const TCHAR* Name : {TEXT("Man"), TEXT("Woman")})
 {
  const FString Path = FString::Printf(TEXT("/Game/Anim_Logic/ABP_Player_%s.ABP_Player_%s_C"), Name, Name);
  UClass* Class = LoadObject<UClass>(nullptr, *Path);
  if (!TestNotNull(*Path, Class)) continue;
  UObject* Defaults = Class->GetDefaultObject();
  int32 OverlayCount = 0, ConnectedRootCount = 0;
  for (TFieldIterator<FStructProperty> It(Class); It; ++It)
  {
   if (It->Struct == FAnimNode_SWCannonOperator::StaticStruct())
   {
    ++OverlayCount;
    const auto* Node = It->ContainerPtrToValuePtr<FAnimNode_SWCannonOperator>(Defaults);
    TestTrue(TEXT("Cannon overlay retains its locomotion input"), Node->ComponentPose.LinkID != INDEX_NONE);
   }
   if (It->Struct == FAnimNode_Root::StaticStruct())
   {
    const auto* Node = It->ContainerPtrToValuePtr<FAnimNode_Root>(Defaults);
    AddInfo(FString::Printf(TEXT("%s %s LinkID=%d"), Name, *It->GetName(), Node->Result.LinkID));
    if (Node->Result.LinkID != INDEX_NONE) ++ConnectedRootCount;
   }
  }
  TestEqual(TEXT("Exactly one cannon overlay survives graph compilation"), OverlayCount, 1);
  // Optional animation layer outputs can intentionally be disconnected.
  TestTrue(TEXT("A final pose output retains its input"), ConnectedRootCount > 0);
 }
 return !HasAnyErrors();
}
#endif
