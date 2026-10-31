#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PlayerLifeTestWorld.h"
#include "Components/SWCorpsePoseComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Development/TestInput/SWDevTestInputComponent.h"
#include "HAL/IConsoleManager.h"
#include "Components/SWPlayerRagdollComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "CollisionChannels.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerDeathDevelopmentTest,
	"ArtisticSW.Player.Death.LifeOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerDeathDevelopmentTest::RunTest(const FString& Parameters)
{
	PlayerLifeTests::FWorld Fixture;
	const PlayerLifeTests::FLife Life = Fixture.SpawnLife();
	if (!TestNotNull(TEXT("Mode"), Fixture.Mode) || !Life.Controller || !Life.State || !Life.Player) return false;
	Fixture.Mode->PlayerIndices.Add(Life.Controller, 0);
	UAbilitySystemComponent* ASC = Life.State->GetAbilitySystemComponent();
	UBaseHealthComponent* OldHealth = Life.Player->GetHealthComponent();
	USWDevTestInputComponent* Input = Life.Controller->DevTestInput;
	const int32 OriginalGeneration = Life.Controller->DeathFlowState.WaitingGeneration;
	FSWInventorySlotSnapshot Seed;
	Seed.Tab = static_cast<uint8>(EInventoryTab::Material);
	Seed.SlotIndex = 3;
	Seed.ItemTag = Item_Id_Material_WeaponMaterial_Wood;
	Seed.Count = 7;
	Life.Player->GetInventoryComponent()->RestoreProgressSnapshot({Seed});
	Input->ServerExecuteTest(ESWDevTestAction::KillSelf, 0, 1, Life.Player, OriginalGeneration);
	TestEqual(TEXT("Server rejects development death without an enabled room session"), Life.Player->GetHealthComponent()->GetHealth(), 1000.0f);
	TestTrue(TEXT("Current life request matches"), Input->MatchesCurrentLife(Life.Player, OriginalGeneration));
	TestFalse(TEXT("Null life request rejected"), Input->MatchesCurrentLife(nullptr, OriginalGeneration));
	ABasePlayer* Replacement = Fixture.SpawnPlayer();
	if (!TestNotNull(TEXT("Replacement"), Replacement)) return false;
	TestFalse(TEXT("Another pawn cannot be selected for suicide"), Input->MatchesCurrentLife(Replacement, OriginalGeneration));
	const float Strength = ASC->GetNumericAttributeBase(UBaseAttributeSet::GetStrengthAttribute());
	AActor* Equipment = Fixture.World->SpawnActor<AActor>();
	UStaticMeshComponent* EquipmentMesh = NewObject<UStaticMeshComponent>(Equipment);
	Equipment->SetRootComponent(EquipmentMesh);
	EquipmentMesh->RegisterComponent();
	Equipment->AttachToComponent(Life.Player->GetMesh(), FAttachmentTransformRules::KeepWorldTransform);
	// Exercise DeathStarted separately: an authored death montage may finish later.
	Life.Player->HandleDeathStarted(OldHealth);
	TestFalse(TEXT("Corpse remains visible at death start"), Life.Player->IsHidden());
	TestEqual(TEXT("Movement capsule cannot block the living player"), Life.Player->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestTrue(TEXT("Attached equipment disappears with the player"), Equipment->IsHidden());
	TestTrue(TEXT("Dead mesh enters ragdoll physics"), Life.Player->GetMesh()->IsAnySimulatingPhysics());
	TestEqual(TEXT("Invisible player cannot keep walking on the ship"), Life.Player->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
	ASC->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), 0.0f);
	TestTrue(TEXT("Existing death pipeline completes"), OldHealth->GetDeathState() == EBaseDeathState::DeathFinished);
	Life.Player->ApplyLocalDeathRagdoll();
	TestFalse(TEXT("Duplicate death keeps corpse visible"), Life.Player->IsHidden());
	TestTrue(TEXT("Death completion retains the existing physical corpse"), Life.Player->GetMesh()->IsAnySimulatingPhysics());
	TestTrue(TEXT("Death tag remains on persistent ASC until new life"), ASC->HasMatchingGameplayTag(State_Dead));
	TestNull(TEXT("GameMode releases possession after capture"), Life.Controller->GetPawn());
	TestFalse(TEXT("Corpse no longer subscribes to shared health"),
		ASC->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetHealthAttribute()).IsBoundToObject(OldHealth));
	const FGameplayTag InteractionTags[] = {Interaction_PickUp, Interaction_ShipBoard, Interaction_CannonBoard};
	for (const FGameplayTag& Tag : InteractionTags)
		if (const auto* Callback = ASC->GenericGameplayEventCallbacks.Find(Tag))
			TestFalse(TEXT("Corpse interaction callback retired"), Callback->IsBoundToObject(Life.Player));
	TestFalse(TEXT("A request queued before death is rejected by life generation"),
		Input->MatchesCurrentLife(Life.Player, OriginalGeneration));

	// Exercise individual-respawn possession with the real captured snapshot.
	Fixture.Mode->IndividualRespawnInProgress.Add(Life.Controller);
	Life.Controller->Possess(Replacement);
	Fixture.Mode->IndividualRespawnInProgress.Remove(Life.Controller);
	TestTrue(TEXT("New life applies the captured progress successfully"), Life.Controller->WasLastLifeProgressApplySuccessful(Replacement));
	TestTrue(TEXT("New life leaves initialization with positive health"),
		!Replacement->GetHealthComponent()->IsLifeInitializing() && !Replacement->GetHealthComponent()->IsDead()
		&& Replacement->GetHealthComponent()->GetHealth() > 0);
	TestFalse(TEXT("New life clears persistent dead tag"), ASC->HasMatchingGameplayTag(State_Dead));
	TestFalse(TEXT("Replacement player is visible"), Replacement->IsHidden());
	TestTrue(TEXT("Replacement mesh remains visible"), Replacement->GetMesh()->IsVisible());
	TestEqual(TEXT("Respawn preserves strength"), ASC->GetNumericAttributeBase(UBaseAttributeSet::GetStrengthAttribute()), Strength);
	TestEqual(TEXT("Respawn restores a nonempty inventory"), Replacement->GetInventoryComponent()->GetItemCount(Seed.ItemTag), Seed.Count);
	TestEqual(TEXT("Respawn preserves original material slot"), Replacement->GetInventoryComponent()->GetSlots(EInventoryTab::Material)[Seed.SlotIndex].Count, Seed.Count);
	Life.Player->SetPlayerState(Life.State);
	Life.Player->OnRep_PlayerState();
	TestEqual(TEXT("Late corpse PlayerState cannot reclaim the ASC"), ASC->GetAvatarActor(), static_cast<AActor*>(Replacement));
	TestFalse(TEXT("Late corpse PlayerState cannot bind the retired health component"),
		ASC->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetHealthAttribute()).IsBoundToObject(OldHealth));
	TestTrue(TEXT("Replacement health remains subscribed"),
		ASC->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetHealthAttribute()).IsBoundToObject(Replacement->GetHealthComponent()));
	for (const FGameplayTag& Tag : InteractionTags)
	{
		const auto* Callback = ASC->GenericGameplayEventCallbacks.Find(Tag);
		TestTrue(TEXT("Late corpse update preserves replacement interaction binding"), Callback && Callback->IsBoundToObject(Replacement));
	}
	Life.Player->HandleDeathFinished(OldHealth);
	TestTrue(TEXT("Duplicate corpse death leaves the replacement possessed"), Life.Controller->GetPawn() == Replacement);
	TestFalse(TEXT("Old-pawn request cannot target replacement"),
		Input->MatchesCurrentLife(Life.Player, Life.Controller->DeathFlowState.WaitingGeneration));
	TestTrue(TEXT("New-pawn request uses the new life"),
		Input->MatchesCurrentLife(Replacement, Life.Controller->DeathFlowState.WaitingGeneration));
	Life.Player->Destroy();
	TestEqual(TEXT("Corpse destruction leaves the living ASC avatar intact"), ASC->GetAvatarActor(), static_cast<AActor*>(Replacement));
	TestTrue(TEXT("Corpse destruction preserves new health binding"),
		ASC->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetHealthAttribute()).IsBoundToObject(Replacement->GetHealthComponent()));
	TestEqual(TEXT("Corpse destruction cannot remove restored inventory"), Replacement->GetInventoryComponent()->GetItemCount(Seed.ItemTag), Seed.Count);
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	TestNotNull(TEXT("Existing development session command"), IConsoleManager::Get().FindConsoleObject(TEXT("SW.DevTest.Session")));
	TestNull(TEXT("Legacy global session variable removed"), IConsoleManager::Get().FindConsoleVariable(TEXT("SW.DevTest.Session")));
	TestNotNull(TEXT("Suicide alias registered"), IConsoleManager::Get().FindConsoleObject(TEXT("sw.DevTest.Suicide")));
#endif
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerRagdollPresentationTest,
    "ArtisticSW.Player.Death.RagdollPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerRagdollPresentationTest::RunTest(const FString& Parameters)
{
    PlayerLifeTests::FWorld Fixture;
    const PlayerLifeTests::FLife Life=Fixture.SpawnLife();
    if(!Life.Player||!Life.Controller)return false;
    auto* Player=Life.Player;auto* Mesh=Player->GetMesh();
    Mesh->SetRelativeScale3D(FVector(1.1f));
    const FTransform Initial=Mesh->GetRelativeTransform();
    const FName InitialProfile=Mesh->GetCollisionProfileName();
    const bool InitialMainPass=Mesh->bRenderInMainPass;
    const ECollisionEnabled::Type InitialCapsule=Player->GetCapsuleComponent()->GetCollisionEnabled();
    Mesh->SetGenerateOverlapEvents(true);Mesh->SetEnableGravity(false);
    AActor* Equipment=Fixture.World->SpawnActor<AActor>();
    auto* Root=NewObject<UStaticMeshComponent>(Equipment);Equipment->SetRootComponent(Root);Root->RegisterComponent();
    Equipment->AttachToComponent(Mesh,FAttachmentTransformRules::KeepWorldTransform);
    Player->GetCharacterMovement()->Velocity=FVector(400,30,0);
    Player->ApplyLocalDeathRagdoll();
    auto* Pose=Player->FindComponentByClass<USWCorpsePoseComponent>();
    if(!TestNotNull(TEXT("Normal death automatically installs corpse skin correction"),Pose))return false;
    const TArray<FTransform> OriginalPose=Mesh->GetComponentSpaceTransforms();
    Pose->TickComponent(1.f/60,LEVELTICK_All,nullptr);
    TestNotNull(TEXT("Corrected visual uses the player skin"),Pose->Visual.Get());
    TestFalse(TEXT("Source mesh does not double-render under corrected skin"),Mesh->bRenderInMainPass);
    TestEqual(TEXT("Correction retains every physical source bone transform"),Mesh->GetComponentSpaceTransforms().Num(),OriginalPose.Num());
    for(int32 I=0;I<OriginalPose.Num();++I)if(!OriginalPose[I].Equals(Mesh->GetComponentSpaceTransforms()[I],.0001)){AddError(TEXT("Skin correction modified source physics pose"));break;}
    TestTrue(TEXT("Physical corpse visible"),!Player->IsHidden()&&Mesh->IsVisible()&&Mesh->IsAnySimulatingPhysics());
    TestTrue(TEXT("Life flow still owns possession"),Life.Controller->GetPawn()==Player);
    TestEqual(TEXT("Living pawns cannot be pushed by corpse"),Mesh->GetCollisionResponseToChannel(ECC_Pawn),ECR_Ignore);
    TestEqual(TEXT("Corpse does not obstruct camera"),Mesh->GetCollisionResponseToChannel(ECC_Camera),ECR_Ignore);
    TestEqual(TEXT("Corpse is not an animated weapon target"),Mesh->GetCollisionResponseToChannel(ECC_WeaponAim),ECR_Ignore);
    TestEqual(TEXT("Moving ship remains a support surface"),Mesh->GetCollisionResponseToChannel(ECC_WorldDynamic),ECR_Block);
    TestTrue(TEXT("Final animation is held during physics"),Mesh->bPauseAnims);
    FBodyInstance* Pelvis=Mesh->GetBodyInstance(TEXT("pelvis"));
    if(!TestNotNull(TEXT("Pelvis body"),Pelvis))return false;
    const FVector Velocity=Pelvis->GetUnrealWorldVelocity();
    TestTrue(TEXT("Movement momentum inherited"),Velocity.Equals(FVector(400,30,0),.1));
    Player->PrepareDeathMovement();
    TestTrue(TEXT("Death ability reentry cannot disable corpse world collision"),Player->GetActorEnableCollision()&&Mesh->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics);
    Player->ApplyLocalDeathRagdoll();
    TestTrue(TEXT("Duplicate entry cannot reset or add momentum"),Pelvis->GetUnrealWorldVelocity().Equals(Velocity,.01));
    Player->ResetLocalDeathRagdoll();
    TestEqual(TEXT("Reset immediately restores original rendering"),Mesh->bRenderInMainPass,InitialMainPass);
    TestFalse(TEXT("Reset hides corrected corpse skin"),Pose->Visual->IsVisible());
    TestFalse(TEXT("Living character does not keep ticking corpse skin correction"),Pose->IsComponentTickEnabled());
    TestFalse(TEXT("Reset ends all simulation"),Mesh->IsAnySimulatingPhysics());
    TestFalse(TEXT("Reset restores animation evaluation"),Mesh->bPauseAnims);
    TestTrue(TEXT("Reset restores living overlap policy"),Mesh->GetGenerateOverlapEvents());
    TestFalse(TEXT("Reset restores authored gravity flag"),Mesh->IsGravityEnabled());
    TestFalse(TEXT("Reset restores animation physics blend policy"),Mesh->bBlendPhysics);
    TestTrue(TEXT("Reset preserves mesh bind/component scale and attachment offset"),Mesh->GetRelativeTransform().Equals(Initial,.001));
    TestEqual(TEXT("Reset restores living collision profile"),Mesh->GetCollisionProfileName(),InitialProfile);
    TestEqual(TEXT("Reset restores capsule state"),Player->GetCapsuleComponent()->GetCollisionEnabled(),InitialCapsule);
    TestFalse(TEXT("Reset restores attached equipment visibility"),Equipment->IsHidden());
    TestTrue(TEXT("Reset restores attached equipment collision"),Equipment->GetActorEnableCollision());
    TestFalse(TEXT("Presentation reset cannot retire the living ASC"),Life.State->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Dead));
    return !HasAnyErrors();
}
#endif
