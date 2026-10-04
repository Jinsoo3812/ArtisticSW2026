#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING && !UE_BUILD_TEST

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "BasePlayerState.h"
#include "Components/BaseHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Settings_Item.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerDeathDevelopmentTest,
	"ArtisticSW.Player.Death.DevelopmentCommand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerDeathDevelopmentTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* Session = IConsoleManager::Get().FindConsoleVariable(TEXT("sw.DevTest.Session"));
	if (!TestNotNull(TEXT("Development session switch registered"), Session)) return false;
	TestNotNull(TEXT("Suicide command registered"), IConsoleManager::Get().FindConsoleObject(TEXT("sw.DevTest.Suicide")));
	const int32 PreviousSession = Session->GetInt();
	ON_SCOPE_EXIT { Session->Set(PreviousSession, ECVF_SetByCode); };

	TGuardValue<TSoftObjectPtr<UDataTable>> CraftingTableGuard(GetMutableDefault<USettings_Item>()->CraftingRecipeDataTable, {});
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("PlayerDeathDevelopmentTest"));
	if (!TestNotNull(TEXT("Transient game world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	// Initialize actors so native dynamic death callbacks execute and the
	// controller creates its real camera manager, while leaving BeginPlay deferred.
	World->InitializeActorsForPlay(FURL());
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ABasePlayerController* Controller = World->SpawnActor<ABasePlayerController>(Spawn);
	ABasePlayerState* State = World->SpawnActor<ABasePlayerState>(Spawn);
	ABasePlayer* Player = World->SpawnActor<ABasePlayer>(Spawn);
	ABasePlayer* OtherPlayer = World->SpawnActor<ABasePlayer>(Spawn);
	if (!Controller || !State || !Player || !OtherPlayer) return false;
	// This transient world does not run BeginPlay; explicitly register the
	// PlayerState attribute subobject as the real game initialization does.
	UAbilitySystemComponent* ASC = State->GetAbilitySystemComponent();
	ASC->AddAttributeSetSubobject(State->GetAttributeSet());
	Controller->PlayerState = State;
	Controller->Possess(Player);
	UBaseHealthComponent* Health = Player->GetHealthComponent();
	if (!TestNotNull(TEXT("Possession initializes the persistent ASC"), ASC)
		|| !TestNotNull(TEXT("Player health"), Health)) return false;
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/jiwon/Characters/SKM_Player_Woman.SKM_Player_Woman"));
	if (!TestNotNull(TEXT("Woman mesh loads with its assigned PhysicsAsset"), Mesh)) return false;
	Player->GetMesh()->SetSkeletalMesh(Mesh);
	// Bind the same gameplay handler as BeginPlay without starting unrelated world systems.
	Health->OnDeathFinished.AddUniqueDynamic(Player, &ABasePlayer::HandleDeathFinished);

	Session->Set(0, ECVF_SetByCode);
	TestTrue(TEXT("Server opt-in is required"),
		Controller->TryStartDevelopmentDeath(Player) == EDevelopmentDeathTestResult::Disabled);
	TestEqual(TEXT("Disabled request preserves health"), Health->GetHealth(), 100.0f);
	Session->Set(1, ECVF_SetByCode);
	TestTrue(TEXT("Cannot kill a different player's pawn"),
		Controller->TryStartDevelopmentDeath(OtherPlayer) == EDevelopmentDeathTestResult::InvalidPawn);
	TestTrue(TEXT("Null/stale pawn is rejected"),
		Controller->TryStartDevelopmentDeath(nullptr) == EDevelopmentDeathTestResult::InvalidPawn);
	Controller->SetRole(ROLE_SimulatedProxy);
	TestTrue(TEXT("Client cannot execute server policy locally"),
		Controller->TryStartDevelopmentDeath(Player) == EDevelopmentDeathTestResult::NotAuthority);
	Controller->SetRole(ROLE_Authority);
	World->WorldType = EWorldType::Editor;
	TestTrue(TEXT("Editor world is rejected"),
		Controller->TryStartDevelopmentDeath(Player) == EDevelopmentDeathTestResult::InvalidWorld);
	World->WorldType = EWorldType::Game;

	TestTrue(TEXT("Own living pawn enters existing death pipeline"),
		Controller->TryStartDevelopmentDeath(Player) == EDevelopmentDeathTestResult::Started);
	TestEqual(TEXT("Suicide sets actual health to zero"), Health->GetHealth(), 0.0f);
	TestTrue(TEXT("Existing health component finishes death without a montage"),
		Health->GetDeathState() == EBaseDeathState::DeathFinished);
	TestTrue(TEXT("Persistent ASC receives the existing death tag"), ASC->HasMatchingGameplayTag(State_Dead));
	TestFalse(TEXT("Suicide does not invent a lethal-hit direction"), Health->GetDeathRagdollImpactData().bHasDirection);
	TestTrue(TEXT("Death completion enables actual mesh physics"), Player->GetMesh()->IsSimulatingPhysics());
	TestFalse(TEXT("Corpse retires persistent ASC health subscriptions"),
		ASC->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetHealthAttribute()).IsBoundToObject(Health));
	if (const auto* Callback = ASC->GenericGameplayEventCallbacks.Find(Interaction_PickUp))
	{
		TestFalse(TEXT("Corpse retires pickup event subscriptions"), Callback->IsBoundToObject(Player));
	}
	TestEqual(TEXT("Ragdoll preserves the controller until GameMode registration"), Player->GetController(), static_cast<AController*>(Controller));
	TestEqual(TEXT("Ragdoll preserves PlayerState for progress capture"), Player->GetPlayerState(), static_cast<APlayerState*>(State));
	Player->ApplyLocalDeathRagdoll();
	TestEqual(TEXT("Repeated ragdoll does not release ownership"), Player->GetController(), static_cast<AController*>(Controller));
	TestTrue(TEXT("Repeated death request is idempotent"),
		Controller->TryStartDevelopmentDeath(Player) == EDevelopmentDeathTestResult::AlreadyDead);

	// Replace the pawn while retaining PlayerState/ASC, as the respawn system does.
	Controller->UnPossess();
	TestEqual(TEXT("Death unpossession retains the corpse camera"), Controller->GetViewTarget(), static_cast<AActor*>(Player));
	TestEqual(TEXT("Controller records the replicated death view"), Controller->GetDeathViewTarget(), Player);
	TestFalse(TEXT("Unpossession does not destroy the corpse"), Player->IsActorBeingDestroyed());
	Controller->Possess(OtherPlayer);
	TestEqual(TEXT("Respawn returns the camera to the new pawn"), Controller->GetViewTarget(), static_cast<AActor*>(OtherPlayer));
	TestNull(TEXT("New possession clears the death view state"), Controller->GetDeathViewTarget());
	TestTrue(TEXT("New avatar binds the persistent health stream"),
		ASC->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetHealthAttribute()).IsBoundToObject(OtherPlayer->GetHealthComponent()));
	Player->SetPlayerState(State);
	Player->OnRep_PlayerState(); // Delayed corpse replication after the replacement is active.
	TestEqual(TEXT("Late corpse PlayerState cannot reclaim the ASC avatar"), ASC->GetAvatarActor(), static_cast<AActor*>(OtherPlayer));
	TestFalse(TEXT("Late corpse PlayerState cannot rebind old health"),
		ASC->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetHealthAttribute()).IsBoundToObject(Health));
	TestFalse(TEXT("New possession clears the persistent dead tag"), ASC->HasMatchingGameplayTag(State_Dead));
	TestTrue(TEXT("New pawn stays alive instead of dying during ASC initialization"),
		OtherPlayer->GetHealthComponent()->GetDeathState() == EBaseDeathState::NotDead);
	TestTrue(TEXT("Queued old request cannot kill the replacement pawn"),
		Controller->TryStartDevelopmentDeath(Player) == EDevelopmentDeathTestResult::InvalidPawn);
	TestEqual(TEXT("Replacement pawn health is unchanged"), OtherPlayer->GetHealthComponent()->GetHealth(), 100.0f);
	TestTrue(TEXT("Command works again for the newly possessed pawn"),
		Controller->TryStartDevelopmentDeath(OtherPlayer) == EDevelopmentDeathTestResult::Started);
	return !HasAnyErrors();
}

#endif
