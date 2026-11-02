#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PlayerLifeTestWorld.h"
#include "Cannon.h"
#include "CannonOperatorStationProfile.h"
#include "Components/SceneComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "CollisionChannels.h"
#include "Animation/MotionMatchingAnimInstance.h"
#include "Animation/LocomotionAnimStateComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimNode_SWCannonOperator.h"
#include "SwimmingComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCannonOperatorLifecycleTest,
	"ArtisticSW.Cannon.Operator.YawPitchVisibilityAndExit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCannonOperatorLifecycleTest::RunTest(const FString& Parameters)
{
	PlayerLifeTests::FWorld Fixture;
	auto First=Fixture.SpawnLife();
	auto Second=Fixture.SpawnLife();
	if (!First.Controller || !Second.Controller || !First.Player || !Second.Player) return false;
	First.Controller->SetAsLocalPlayerController(); Second.Controller->SetAsLocalPlayerController();
	First.Player->SetMountedDamageMode(true);
	TestFalse(TEXT("A shared mounted damage mode does not activate cannon animation"),First.Player->IsOperatingCannon());
	First.Player->SetMountedDamageMode(false);
	UCannonOperatorStationProfile* Station=NewObject<UCannonOperatorStationProfile>();
	Station->Mount=FTransform(FVector(-148,0,54)); Station->Exit=Station->Mount;
	TGuardValue<TObjectPtr<UCannonOperatorStationProfile>> StationGuard(GetMutableDefault<ACannon>()->OperatorStationProfile,Station);
	ACannon* Cannon=Fixture.World->SpawnActor<ACannon>();
	if (!Cannon) return false;
	TInlineComponentArray<UStaticMeshComponent*> GunMeshes(Cannon);
	GunMeshes.RemoveAll([](const UStaticMeshComponent* Component)
	{
		return Component->GetFName()!=TEXT("BaseMesh") && Component->GetFName()!=TEXT("BarrelMesh");
	});
	TestEqual(TEXT("The ground policy covers the base and barrel"),GunMeshes.Num(),2);
	UStaticMeshComponent* ContactSurface=NewObject<UStaticMeshComponent>(Cannon,TEXT("OperatorBarrelVisual"));
	ContactSurface->ComponentTags.Add(TEXT("CannonOperatorContact"));
	ContactSurface->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Blueprints/Ship/Cannon/Mesh/Operator/SM_OperatorBarrelVisual.SM_OperatorBarrelVisual")));
	Cannon->AddInstanceComponent(ContactSurface);
	for (UStaticMeshComponent* GunMesh : GunMeshes) if(GunMesh->GetFName()==TEXT("BarrelMesh")) ContactSurface->SetupAttachment(GunMesh);
	ContactSurface->RegisterComponent();
	for (UStaticMeshComponent* GunMesh : GunMeshes) GunMesh->SetCollisionResponseToChannel(ECC_FootPlacement,ECR_Block);
	for (UStaticMeshComponent* GunMesh : GunMeshes) if(GunMesh->GetFName()==TEXT("BaseMesh"))
		Cannon->GetOperatorTarget(0)->GetAttachParent()->AttachToComponent(GunMesh,FAttachmentTransformRules::KeepRelativeTransform);
	Cannon->GetOperatorTarget(0)->GetAttachParent()->SetRelativeLocation(FVector(-900,0,0));
	Cannon->DispatchBeginPlay();
	TestTrue(TEXT("Class station profile replaces stale ship child-actor component overrides"),
		Cannon->GetOperatorTarget(0)->GetComponentLocation().Equals(Station->Mount.TransformPosition(Station->LeftFoot),.01));
	TestEqual(TEXT("A stale rotating mount parent is replaced with the fixed operator frame"),
		Cannon->GetOperatorTarget(0)->GetAttachParent()->GetAttachParent()->GetFName(),FName(TEXT("OperatorPivot")));
	TestTrue(TEXT("Authored mesh sockets own the left and right palm contacts"),
		Cannon->GetOperatorTarget(2)->GetAttachParent()==ContactSurface && Cannon->GetOperatorTarget(3)->GetAttachParent()==ContactSurface
		&& Cannon->GetOperatorTarget(2)->GetAttachSocketName()==Station->LeftPalmSocket && Cannon->GetOperatorTarget(3)->GetAttachSocketName()==Station->RightPalmSocket);
	for (UStaticMeshComponent* GunMesh : GunMeshes)
		TestEqual(FString::Printf(TEXT("Authored blocking profile cannot turn %s into foot IK ground"),*GunMesh->GetName()),
			GunMesh->GetCollisionResponseToChannel(ECC_FootPlacement),ECR_Ignore);
	Cannon->Board(First.Player);
	TestTrue(TEXT("Only the actual rider activates cannon presentation"),First.Player->IsOperatingCannon());
	TestFalse(TEXT("Another player keeps normal combat and locomotion presentation"),Second.Player->IsOperatingCannon());
	UMotionMatchingAnimInstance* Presentation=NewObject<UMotionMatchingAnimInstance>(First.Player->GetMesh());
	Presentation->CachedBasePlayer=First.Player;
	Presentation->CachedLocomotionStateComponent=First.Player->FindComponentByClass<ULocomotionAnimStateComponent>();
	Presentation->MainChooserTable=nullptr;
	Presentation->StateControllerPlaybackHoldState=EStateControllerPresentationState::TurnInPlace;
	Presentation->StateControllerPlaybackHoldDuration=2.f;
	Presentation->StateControllerSelectedAnimation=NewObject<UAnimSequence>();
	Presentation->StateControllerActiveTurnInPlaceIndex=3;
	Presentation->EvaluateStateControllerPresentationState();
	TestTrue(TEXT("Mounting retires a hidden held turn clip"),Presentation->StateControllerPlaybackHoldState==EStateControllerPresentationState::IdleLoop);
	TestEqual(TEXT("Mounting clears the previous turn bucket"),Presentation->StateControllerActiveTurnInPlaceIndex,0);
	Cannon->Tick(.3f);
	TestTrue(TEXT("Only the cannon controller hides its operator"), Cannon->IsOperatorHiddenForController(First.Controller));
	TestFalse(TEXT("Another local view still sees the operator"), Cannon->IsOperatorHiddenForController(Second.Controller));
	const FVector FootBefore=Cannon->GetOperatorTarget(0)->GetComponentLocation();
	const FVector HandBefore=Cannon->GetOperatorTarget(2)->GetComponentLocation();
	Cannon->SetAIAimRotation(0.f,60.f); Cannon->Tick(.3f);
	const FVector HandAfterYaw=Cannon->GetOperatorTarget(2)->GetComponentLocation();
	TestFalse(TEXT("Yaw moves the palm contact with the barrel socket"),HandAfterYaw.Equals(HandBefore,.01));
	TestTrue(TEXT("Yaw leaves the operator's stance planted"),
		FMath::Abs(First.Player->GetActorRotation().Yaw)<.1f && Cannon->GetOperatorTarget(0)->GetComponentLocation().Equals(FootBefore,.01));
	Cannon->SetAIAimRotation(45.f,60.f); Cannon->Tick(.3f);
	TestTrue(TEXT("Pitch keeps the foot target on the deck frame"), Cannon->GetOperatorTarget(0)->GetComponentLocation().Equals(FootBefore,.01));
	TestFalse(TEXT("Pitch moves the palm contact with the barrel socket"), Cannon->GetOperatorTarget(2)->GetComponentLocation().Equals(HandAfterYaw,.01));
	TestTrue(TEXT("Body never pitches with the barrel"), FMath::Abs(First.Player->GetActorRotation().Pitch)<.01f);
	First.Player->GetCharacterMovement()->GetPredictionData_Server_Character()->CurrentClientTimeStamp=12.f;
	Presentation->StateControllerPlaybackHoldState=EStateControllerPresentationState::TurnInPlace;
	Presentation->StateControllerPlaybackHoldDuration=2.f;
	Presentation->StateControllerSelectedAnimation=NewObject<UAnimSequence>();
	Cannon->ForceExit(); Cannon->Tick(.3f);
	TestFalse(TEXT("Cannon presentation ends on dismount"),First.Player->IsOperatingCannon());
	Presentation->EvaluateStateControllerPresentationState();
	TestTrue(TEXT("Dismount does not replay the pre-mount turn"),Presentation->StateControllerPlaybackHoldState!=EStateControllerPresentationState::TurnInPlace);
	TestEqual(TEXT("Exit discards the previous movement prediction timeline"),
		First.Player->GetCharacterMovement()->GetPredictionData_Server_Character()->CurrentClientTimeStamp,0.f);
	TestTrue(TEXT("Exit restores character possession"), First.Controller->GetPawn()==First.Player);
	TestTrue(TEXT("Exit view faces the character without an unintended turn request"),
		FMath::Abs(FMath::FindDeltaAngleDegrees(First.Controller->GetControlRotation().Yaw,First.Player->GetActorRotation().Yaw))<.1f);
	TestTrue(TEXT("Exit detaches the character"), First.Player->GetAttachParentActor()==nullptr);
	TestFalse(TEXT("Exit removes only this cannon's view hide"), First.Controller->HiddenActors.Contains(First.Player));
	for (const EMovementMode Mode : {MOVE_Falling,MOVE_Swimming,MOVE_Custom})
	{
		FAnimNode_SWCannonOperator ReleasePose;
		ReleasePose.MountWeight=.8f; ReleasePose.bHasTargets=true; ReleasePose.Alpha=1.f;
		ReleasePose.bCachedMounted=true; ReleasePose.bReachInitialized=true;
		ReleasePose.bHeadAimInitialized=true;
		First.Player->GetCharacterMovement()->SetMovementMode(Mode,Mode==MOVE_Custom ? uint8(ECustomMovementMode::CMOVE_Swimming) : 0);
		if (Mode==MOVE_Falling)
		{
			ReleasePose.PreUpdate(Presentation);
			TestEqual(TEXT("A brief floor probe after possession preserves the exit blend"),ReleasePose.Alpha,1.f);
			First.Player->Jump();
		}
		ReleasePose.PreUpdate(Presentation);
		TestEqual(TEXT("A jump or swim immediately owns the released pose"),ReleasePose.Alpha,0.f);
		TestFalse(TEXT("No stale cannon endpoints survive a movement interruption"),ReleasePose.bHasTargets);
		TestFalse(TEXT("The next mount starts a fresh contact solve"),ReleasePose.bReachInitialized);
		TestFalse(TEXT("Movement interruption discards the mounted neck filter"),ReleasePose.bHeadAimInitialized);
		First.Player->StopJumping();
	}
	First.Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	ACannon* OtherCannon=Fixture.World->SpawnActor<ACannon>();
	OtherCannon->Board(Second.Player); OtherCannon->Tick(.3f);
	OtherCannon->Destroy();
	TestTrue(TEXT("Cannon destruction restores possession before APawn clears its controller"), Second.Controller->GetPawn()==Second.Player);
	TestTrue(TEXT("Destruction detaches the operator"), Second.Player->GetAttachParentActor()==nullptr);
	TestFalse(TEXT("Destruction restores local visibility"), Second.Controller->HiddenActors.Contains(Second.Player));
	return !HasAnyErrors();
}
#endif
