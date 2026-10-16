#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Settings_Item.h"
#include "Audio/SWFoleyAudioProfile.h"
#include "Components/SWFoleyComponent.h"
#include "System/SWFoleySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameplayTagsManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Sound/SoundWave.h"
#include <limits>
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "Animation/AnimSequence.h"
#include "Animation/SWAnimNotify_FoleyEvent.h"
#endif

namespace
{
constexpr auto FoleyTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWFoleySurfaceTest, "SW.Foley.SurfaceFallback", FoleyTestFlags)
bool FSWFoleySurfaceTest::RunTest(const FString&)
{
	auto* Profile = NewObject<USWFoleyAudioProfile>();
	auto* Default = NewObject<USoundWave>(Profile, TEXT("DefaultContact"));
	auto* Stone = NewObject<USoundWave>(Profile, TEXT("StoneContact"));
	FSWFoleySoundSet Set;
	Set.DefaultSound = Default;
	Set.SurfaceSounds.Add(SurfaceType1, Stone);
	Set.SurfaceSounds.Add(SurfaceType2, TSoftObjectPtr<USoundBase>());
	TestEqual(TEXT("Defined surface selects its own sound"), Set.ResolveSound(SurfaceType1).Get(), static_cast<USoundBase*>(Stone));
	TestEqual(TEXT("Unknown surface falls back"), Set.ResolveSound(SurfaceType3).Get(), static_cast<USoundBase*>(Default));
	TestEqual(TEXT("Empty surface override also falls back"), Set.ResolveSound(SurfaceType2).Get(), static_cast<USoundBase*>(Default));
	TestEqual(TEXT("No physical material uses default"), Set.ResolveSound(SurfaceType_Default).Get(), static_cast<USoundBase*>(Default));
	Profile->Events.Add(SW::Foley::Walk, Set);
	Profile->Events.Add(SW::Foley::RunStrafe, Set);
	TestNull(TEXT("RunStrafe does not accidentally resolve as Run"), Profile->Events.Find(SW::Foley::Run));
	TArray<FSoftObjectPath> Paths;
	Profile->GatherSoundPaths(Paths);
	TestEqual(TEXT("Shared clip references are preloaded once"), Paths.Num(), 2);
	Set.DefaultSound.Reset(); Set.SurfaceSounds.Reset();
	TestFalse(TEXT("Missing audio remains a silent, valid authoring state"), Set.HasSound());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWFoleyFallbackTest, "SW.Foley.EventFallbackRouting", FoleyTestFlags)
bool FSWFoleyFallbackTest::RunTest(const FString&)
{
	auto* Profile = NewObject<USWFoleyAudioProfile>();
	FSWFoleySoundSet Run; Run.DefaultSound = NewObject<USoundWave>(Profile);
	Profile->Events.Add(SW::Foley::Run, Run);
	FGameplayTag Resolved = SW::Foley::Walk;
	TestNull(TEXT("Variants never implicitly borrow another event"), Profile->ResolveEvent(SW::Foley::RunStrafe, &Resolved));
	TestFalse(TEXT("Failed resolution clears output tag"), Resolved.IsValid());
	Profile->EventFallbacks.Add(SW::Foley::RunStrafe, SW::Foley::RunBackwards);
	Profile->EventFallbacks.Add(SW::Foley::RunBackwards, SW::Foley::Run);
	TestEqual(TEXT("Explicit multi-hop fallback reaches populated definition"), Profile->ResolveEvent(SW::Foley::RunStrafe, &Resolved),
		static_cast<const FSWFoleySoundSet*>(Profile->Events.Find(SW::Foley::Run)));
	TestEqual(TEXT("Resolved event identifies shared cooldown/contact policy"), Resolved, SW::Foley::Run.GetTag());
	FSWFoleySoundSet Exact; Exact.SurfaceSounds.Add(SurfaceType1, NewObject<USoundWave>(Profile));
	Exact.Contact = ESWFoleyContact::Hand;
	Profile->Events.Add(SW::Foley::RunStrafe, Exact);
	TestTrue(TEXT("Surface-only exact definitions retain their contact policy"),
		Profile->ResolveEvent(SW::Foley::RunStrafe)->Contact == ESWFoleyContact::Hand);
	Exact.SurfaceSounds.Reset();
	Exact.DefaultSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Unassigned/ColdSound.ColdSound")));
	Profile->Events.Add(SW::Foley::RunStrafe, Exact);
	TestEqual(TEXT("An authored but unloaded exact clip does not switch policy while streaming"),
		Profile->ResolveEvent(SW::Foley::RunStrafe, &Resolved), static_cast<const FSWFoleySoundSet*>(Profile->Events.Find(SW::Foley::RunStrafe)));
	Profile->Events.Add(SW::Foley::RunStrafe, FSWFoleySoundSet());
	TestNotNull(TEXT("Explicitly empty exact definition follows an authored fallback"), Profile->ResolveEvent(SW::Foley::RunStrafe));
	Profile->EventFallbacks.Add(SW::Foley::RunBackwards, SW::Foley::RunStrafe);
	TestNull(TEXT("A cycle terminates without audio or unbounded traversal"), Profile->ResolveEvent(SW::Foley::RunStrafe));
	Profile->EventFallbacks.Reset();
	TestNull(TEXT("Missing routes stay silent"), Profile->ResolveEvent(SW::Foley::Handplant));

	FGameplayTagContainer Tags;
	UGameplayTagsManager::Get().RequestAllGameplayTags(Tags, true);
	const auto& TagArray = Tags.GetGameplayTagArray();
	if (TestTrue(TEXT("Project tag dictionary provides bounded-graph fixture"), TagArray.Num() > USWFoleyAudioProfile::MaxFallbackDepth))
	{
		Profile->Events.Reset();
		for (int32 I = 0; I < USWFoleyAudioProfile::MaxFallbackDepth; ++I) { Profile->EventFallbacks.Add(TagArray[I], TagArray[I + 1]); }
		Profile->Events.Add(TagArray[USWFoleyAudioProfile::MaxFallbackDepth], Run);
		TestNull(TEXT("Overlong acyclic routes also have a fixed work bound"), Profile->ResolveEvent(TagArray[0]));
		Profile->Events.Add(TagArray[USWFoleyAudioProfile::MaxFallbackDepth - 1], Run);
		TestNotNull(TEXT("Definition at the supported depth remains usable"), Profile->ResolveEvent(TagArray[0]));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWFoleyContactTest, "SW.Foley.ContactPolicies", FoleyTestFlags)
bool FSWFoleyContactTest::RunTest(const FString&)
{
	TGuardValue<TSoftObjectPtr<UDataTable>> CraftingGuard(GetMutableDefault<USettings_Item>()->CraftingRecipeDataTable, {});
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto* Character = World->SpawnActor<ACharacter>();
	auto* Component = NewObject<USWFoleyComponent>(Character); Component->RegisterComponent();
	auto* Profile = NewObject<USWFoleyAudioProfile>(World);
	auto* Mesh = Character->GetMesh();
	auto* Asset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"));
	if (TestNotNull(TEXT("Basic mannequin contact fixture exists"), Asset))
	{
		Mesh->SetSkeletalMeshAsset(Asset);
		Mesh->SetLastRenderTime(World->GetTimeSeconds());
		Mesh->RefreshBoneTransforms();
		TestTrue(TEXT("Fixture provides hand and foot bones"), Mesh->DoesSocketExist(TEXT("hand_l")) && Mesh->DoesSocketExist(TEXT("foot_l")));
		FSWFoleySoundSet Definition;
		Definition.Contact = ESWFoleyContact::Hand;
		TestTrue(TEXT("Hand event selects hand rather than foot"), Component->ResolveContactLocation(*Profile, ESWFoleySide::Left, &Definition)
			.Equals(Mesh->GetSocketLocation(TEXT("hand_l"))));
		Definition.Contact = ESWFoleyContact::Foot;
		TestTrue(TEXT("Foot event retains mannequin contact"), Component->ResolveContactLocation(*Profile, ESWFoleySide::Right, &Definition)
			.Equals(Mesh->GetSocketLocation(TEXT("foot_r"))));
		Definition.Contact = ESWFoleyContact::Socket; Definition.ContactSocket = TEXT("pelvis");
		TestTrue(TEXT("Explicit socket works without a left/right side"), Component->ResolveContactLocation(*Profile, ESWFoleySide::None, &Definition)
			.Equals(Mesh->GetSocketLocation(TEXT("pelvis"))));
		const FVector CapsuleContact = Character->GetActorLocation() - Character->GetActorUpVector() * Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Definition.Contact = ESWFoleyContact::Capsule;
		TestTrue(TEXT("Capsule policy bypasses available mesh bones"), Component->ResolveContactLocation(*Profile, ESWFoleySide::Left, &Definition).Equals(CapsuleContact));
		Definition.Contact = ESWFoleyContact::Socket; Definition.ContactSocket = TEXT("MissingSocket");
		TestTrue(TEXT("Missing explicit contact falls back safely"), Component->ResolveContactLocation(*Profile, ESWFoleySide::None, &Definition).Equals(CapsuleContact));
		Definition.Contact = ESWFoleyContact::Hand;
		Mesh->SetLastRenderTime(-100.f);
		TestTrue(TEXT("Offscreen hand events do not consume stale bones"), Component->ResolveContactLocation(*Profile, ESWFoleySide::Left, &Definition).Equals(CapsuleContact));
	}
	GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWFoleyAdmissionTest, "SW.Foley.CrowdAdmission", FoleyTestFlags)
bool FSWFoleyAdmissionTest::RunTest(const FString&)
{
	using FRequest = USWFoleySubsystem::FRequest;
	FSWFoleyEvent Event; Event.Event = SW::Foley::Walk;
	TestTrue(TEXT("Fresh valid contact accepted"), USWFoleySubsystem::ValidateEvent(Event, 0.15f));
	Event.AgeSeconds = 0.4f;
	TestFalse(TEXT("Old replicated event excluded"), USWFoleySubsystem::ValidateEvent(Event, 0.15f));
	Event.AgeSeconds = 0.f; Event.VolumeMultiplier = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("NaN cannot poison ordering/mix"), USWFoleySubsystem::ValidateEvent(Event, 0.15f));
	Event.VolumeMultiplier = 1.f; Event.Side = static_cast<ESWFoleySide>(255);
	TestFalse(TEXT("Invalid side cannot index cooldown memory"), USWFoleySubsystem::ValidateEvent(Event, 0.15f));
	Event.Side = ESWFoleySide::Left;
	TestTrue(TEXT("Nearby event outranks distant event of same kind"),
		USWFoleySubsystem::ScoreEvent(1.f, 100.f * 100.f, 1800.f) >
		USWFoleySubsystem::ScoreEvent(1.f, 1600.f * 1600.f, 1800.f));
	TArray<FRequest> Requests;
	TArray<USWFoleyComponent*> Emitters;
	for (int32 I = 0; I < 100; ++I)
	{
		auto* Emitter = NewObject<USWFoleyComponent>(); Emitters.Add(Emitter);
		FRequest Request; Request.Component = Emitter; Request.Event = Event; Request.SubmittedAt = 10.0;
		Request.Order = I + 1; Request.Score = 1.f / (I + 1.f); Requests.Add(Request);
	}
	// A local contact is submitted last, with a deliberately lower score than the entire crowd.
	FRequest Local; Local.Component = NewObject<USWFoleyComponent>(); Local.Event = Event;
	Local.SubmittedAt = 10.0; Local.bLocal = true; Local.Score = 0.001f; Local.Order = 101; Requests.Add(Local);
	FRequest Expired = Local; Expired.SubmittedAt = 9.0; Expired.Order = 102; Requests.Add(Expired);
	FRequest Noisy = Requests[0]; Noisy.Order = 103; Noisy.Score = 2.f; Requests.Add(Noisy);
	uint64 ExpiredCount = 0, Dropped = 0;
	auto Selected = USWFoleySubsystem::SelectBatch(Requests, 10.01, 0.15f, 2, 6, ExpiredCount, Dropped);
	TestEqual(TEXT("Local plus six remote slots"), Selected.Num(), 7);
	TestTrue(TEXT("Local contact protected from crowd scores"), Selected[0].bLocal);
	TestEqual(TEXT("Highest remote perceptual priority wins"), Selected[1].Order, uint64(103));
	TestEqual(TEXT("One old snapshot removed"), ExpiredCount, uint64(1));
	TestEqual(TEXT("Unselected events discarded, including duplicate emitter"), Dropped, uint64(95));
	TestEqual(TEXT("There is no delayed playback backlog"), Requests.Num(), 0);
	TSet<USWFoleyComponent*> UniqueRemote;
	for (const auto& Request : Selected) { if (!Request.bLocal) { UniqueRemote.Add(Request.Component.Get()); } }
	TestEqual(TEXT("Noisy emitter cannot monopolize all remote slots"), UniqueRemote.Num(), 6);
	Requests = Selected;
	Selected = USWFoleySubsystem::SelectBatch(Requests, 10.02, 0.15f, 2, 0, ExpiredCount, Dropped);
	TestEqual(TEXT("Exhausted remote rate budget preserves local event"), Selected.Num(), 1);
	TestTrue(TEXT("Local reserve independent from remote token bucket"), Selected[0].bLocal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWFoleyLifetimeTest, "SW.Foley.LifetimeAndMissingContent", FoleyTestFlags)
bool FSWFoleyLifetimeTest::RunTest(const FString&)
{
	TGuardValue<TSoftObjectPtr<UDataTable>> CraftingGuard(GetMutableDefault<USettings_Item>()->CraftingRecipeDataTable, {});
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	auto* Character = World->SpawnActor<ACharacter>();
	auto* Component = NewObject<USWFoleyComponent>(Character);
	Character->AddInstanceComponent(Component); Component->RegisterComponent();
	TestTrue(TEXT("Foley auto-activates on a playing avatar"), Component->IsActive());
	TestFalse(TEXT("Avatar Foley has no per-frame Tick"), Component->PrimaryComponentTick.bCanEverTick);
	TestFalse(TEXT("No per-footstep replication state"), Component->GetIsReplicated());
	FSWFoleyEvent Event; Event.Event = SW::Foley::Walk;
	TestFalse(TEXT("No profile/assets/listener stays silent"), Component->PlayFoleyEvent(Event));
	Component->bEnabled = false;
	TestFalse(TEXT("Disabled avatar cannot present"), Component->CanPresent(ESWFoleyGroup::Other));
	Component->bEnabled = true;
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	TestFalse(TEXT("Airborne walk contacts suppressed"), Component->CanPresent(ESWFoleyGroup::Footstep));
	TestTrue(TEXT("Airborne jump semantic is still allowed"), Component->CanPresent(ESWFoleyGroup::Jump));
	auto* Profile = NewObject<USWFoleyAudioProfile>(World);
	Character->SetActorLocation(FVector(1000.f, 2000.f, 500.f));
	Profile->SourceLeftFoot = TEXT("MissingFoot"); Profile->VisualLeftFoot = TEXT("MissingVisualFoot");
	TestTrue(TEXT("Missing/stale sockets use current capsule ground contact"),
		Component->ResolveContactLocation(*Profile, ESWFoleySide::Left).Equals(
			Character->GetActorLocation() - Character->GetActorUpVector() * Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
	// Cancellation removes only this avatar; end-play drops all remaining weak requests/resources.
	auto* Service = World->GetSubsystem<USWFoleySubsystem>();
	if (TestNotNull(TEXT("Client world creates shared Foley service"), Service))
	{
		// Exercise actual admission without starting a hardware audio device.
		Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		FSWFoleySoundSet Set; Set.DefaultSound = NewObject<USoundWave>(Profile);
		Profile->Events.Add(SW::Foley::Walk, Set); Profile->Events.Add(SW::Foley::Run, Set);
		Component->ProfileOverride = Profile;
		TestFalse(TEXT("Unowned/remote avatars do not preload during ownership hooks"), Component->PrepareLocalAudio());
		TestEqual(TEXT("Remote ownership check runs before allocating stream resources"), Service->Resources.Num(), 0);
		Service->ListenerFrame = GFrameCounter; Service->ListenerPositions.Add(Character->GetActorLocation());
		Event.Side = ESWFoleySide::Left;
		TestTrue(TEXT("Ready profile admits first contact"), Service->Submit(Component, Event));
		Event.Event = SW::Foley::Run;
		TestFalse(TEXT("Walk/Run blend cannot double the same foot contact"), Service->Submit(Component, Event));
		Event.Side = ESWFoleySide::Right;
		TestTrue(TEXT("Opposite foot keeps independent admission"), Service->Submit(Component, Event));
		Service->Pending.Reset();
		for (int32 I = 0; I < USWFoleySubsystem::MaxPending; ++I)
		{
			USWFoleySubsystem::FRequest Filler; Filler.Score = 4.f; Filler.Order = I; Service->Pending.Add(Filler);
		}
		auto* PC = World->SpawnActor<APlayerController>();
		PC->SetAsLocalPlayerController();
		auto* PlayerState = World->SpawnActor<APlayerState>(); PlayerState->SetOwner(PC); PC->SetPlayerState(PlayerState);
		PC->Possess(Character);
		// Possession restarts character movement. Restore the grounded fixture before a foot contact.
		Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		TestTrue(TEXT("Local capacity fixture is a locally controlled player"), Character->IsPlayerControlled() && Character->IsLocallyControlled());
		const int32 PendingBeforeWarmup = Service->Pending.Num();
		TestTrue(TEXT("Local ownership can prepare its shared audio"), Component->PrepareLocalAudio());
		TestTrue(TEXT("Repeated ownership/profile hooks reuse preparation"), Component->PrepareLocalAudio());
		TestEqual(TEXT("Warmup does not enqueue or replay a contact"), Service->Pending.Num(), PendingBeforeWarmup);
		TestEqual(TEXT("Local warmup shares the existing profile cache entry"), Service->Resources.Num(), 1);
		Event.Side = ESWFoleySide::None;
		TestTrue(TEXT("A full crowd queue cannot deny a local player's contact"), Service->Submit(Component, Event));
		TestEqual(TEXT("Queue replacement retains hard capacity"), Service->Pending.Num(), USWFoleySubsystem::MaxPending);
		TestTrue(TEXT("Local request is retained despite lower numerical score"), Service->Pending.ContainsByPredicate(
			[Component](const auto& R) { return R.bLocal && R.Component.Get() == Component; }));
		Service->Pending.Reset();
		USWFoleySubsystem::FRequest Request; Request.Component = Component; Request.Event = Event;
		Service->Pending.Add(Request);
		auto* Other = NewObject<USWFoleyComponent>(Character); Request.Component = Other; Service->Pending.Add(Request);
		Service->Cancel(Component);
		TestEqual(TEXT("Avatar replacement cancels only that avatar"), Service->Pending.Num(), 1);
		Service->Stop();
		TestEqual(TEXT("World shutdown clears queued contacts"), Service->Pending.Num(), 0);
		TestEqual(TEXT("World shutdown releases shared streaming cache"), Service->Resources.Num(), 0);
		TestFalse(TEXT("Late submission after shutdown rejected"), Service->Submit(Component, Event));
	}
	World->EndPlay(EEndPlayReason::Quit);
	GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWFoleyLocomotionAssetTest, "SW.Foley.LocomotionAssetContacts", FoleyTestFlags)
bool FSWFoleyLocomotionAssetTest::RunTest(const FString&)
{
	// A stripped notify has no BP dependency, so a referencer-only migration audit misses it.
	// Cover the actual straight locomotion loops selected by Motion Matching.
	for (const TCHAR* Relative : {TEXT("Run/M_Neutral_Run_Loop_F"),
		TEXT("Sprint/M_Neutral_Sprint_Loop_F")})
	{
		const FString Path = FString(TEXT("/Game/Characters/UEFN_Mannequin/Animations/")) + Relative;
		const UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *Path);
		if (!TestNotNull(*Path, Sequence)) { continue; }
		int32 Contacts = 0;
		bool bLeft = false, bRight = false;
		for (const FAnimNotifyEvent& Event : Sequence->Notifies)
		{
			if (!Event.NotifyName.ToString().StartsWith(TEXT("FoleyEvent"))) { continue; }
			const auto* Notify = Cast<USWAnimNotify_FoleyEvent>(Event.Notify);
			if (!TestNotNull(*(Path + TEXT(" must have executable contacts, not just notify names")), Notify)) { continue; }
			TestTrue(TEXT("Dedicated server does not execute cosmetic contacts"), !Event.bTriggerOnDedicatedServer);
			TestTrue(TEXT("Contact tag is mapped to a locomotion event"),
				Notify->FoleyEvent.Event == SW::Foley::Run || Notify->FoleyEvent.Event == SW::Foley::Walk);
			bLeft |= Notify->FoleyEvent.Side == ESWFoleySide::Left;
			bRight |= Notify->FoleyEvent.Side == ESWFoleySide::Right;
			++Contacts;
		}
		TestTrue(*(Path + TEXT(" contains both authored foot contacts")), Contacts >= 2 && bLeft && bRight);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWFoleyValidationTest, "SW.Foley.ProfileValidation", FoleyTestFlags)
bool FSWFoleyValidationTest::RunTest(const FString&)
{
	auto* Profile = NewObject<USWFoleyAudioProfile>();
	FSWFoleySoundSet Set; Set.DefaultSound = NewObject<USoundWave>(Profile);
	Profile->Events.Add(SW::Foley::Run, Set);
	Profile->EventFallbacks.Add(SW::Foley::RunStrafe, SW::Foley::Run);
	FDataValidationContext Valid;
	TestTrue(TEXT("Usable explicit fallback validates"), Profile->IsDataValid(Valid) == EDataValidationResult::Valid);
	Profile->EventFallbacks.Add(SW::Foley::Run, SW::Foley::RunStrafe);
	FDataValidationContext Cyclic;
	TestTrue(TEXT("Even a currently masked fallback cycle is rejected for authoring"), Profile->IsDataValid(Cyclic) == EDataValidationResult::Invalid);
	Profile->EventFallbacks.Reset();
	Profile->Events[SW::Foley::Run].Contact = ESWFoleyContact::Socket;
	FDataValidationContext MissingSocket;
	TestTrue(TEXT("Explicit contact policy requires a socket name"), Profile->IsDataValid(MissingSocket) == EDataValidationResult::Invalid);
	Profile->Events[SW::Foley::Run].ContactSocket = TEXT("pelvis");
	static_cast<USoundWave*>(Set.DefaultSound.Get())->bLooping = true;
	FDataValidationContext Loop;
	TestTrue(TEXT("Loaded looping clip rejected from one-shot profile"), Profile->IsDataValid(Loop) == EDataValidationResult::Invalid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWFoleyServerTest, "SW.Foley.DedicatedServer", FoleyTestFlags)
bool FSWFoleyServerTest::RunTest(const FString&)
{
	TGuardValue<TSoftObjectPtr<UDataTable>> CraftingGuard(GetMutableDefault<USettings_Item>()->CraftingRecipeDataTable, {});
	UWorld* World = UWorld::CreateWorld(EWorldType::PIE, false);
	GEngine->CreateNewWorldContext(EWorldType::PIE).SetCurrentWorld(World);
	World->SetPlayInEditorInitialNetMode(NM_DedicatedServer);
	TestEqual(TEXT("Fixture is a dedicated server"), World->GetNetMode(), NM_DedicatedServer);
	TestFalse(TEXT("Dedicated worlds cannot create the Foley scheduler"),
		GetDefault<USWFoleySubsystem>()->ShouldCreateSubsystem(World));
	auto* Character = World->SpawnActor<ACharacter>();
	auto* Foley = NewObject<USWFoleyComponent>(Character); Foley->RegisterComponent();
	Foley->ProfileOverride = TSoftObjectPtr<USWFoleyAudioProfile>(FSoftObjectPath(TEXT("/Game/Unassigned/Foley.Foley")));
	FSWFoleyEvent Event; Event.Event = SW::Foley::Land;
	TestFalse(TEXT("Server event rejected before streaming any profile"), Foley->PlayFoleyEvent(Event));
	TestFalse(TEXT("Dedicated server cannot preload even an unresolved profile"), Foley->PrepareLocalAudio());
	TestFalse(TEXT("Server cannot present semantic impacts"), Foley->CanPresent(ESWFoleyGroup::Land));
	GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
	return true;
}
#endif

#endif
