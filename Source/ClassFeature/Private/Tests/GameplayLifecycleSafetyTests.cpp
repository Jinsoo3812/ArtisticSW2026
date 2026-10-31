#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystemComponent.h"
#include "Attacker/GA_Sniping.h"
#include "Attacker/GA_ThrowGrenade.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Crafter/CrafterComponent.h"
#include "Crafter/WorkTable.h"
#include "GAS/SWOwnedLooseGameplayTags.h"
#include "Interact.h"
#include "InteractableComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Misc/AutomationTest.h"
#include "PlayerLifeTestWorld.h"
#include "UObject/UnrealType.h"
#include "Item/BaseItem.h"
#include "ItemData.h"
#include "Projectiles/GrenadeProjectile.h"
#include "EngineUtils.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOwnedLooseTagsTest, "ArtisticSW.Lifecycle.Tags.Ownership",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOwnedLooseTagsTest::RunTest(const FString&)
{
	UAbilitySystemComponent* A = NewObject<UAbilitySystemComponent>();
	UAbilitySystemComponent* B = NewObject<UAbilitySystemComponent>();
	FSWOwnedLooseGameplayTags First, Second;
	A->AddLooseGameplayTag(State_Aiming);
	First.Set(A, State_Aiming, false);
	First.Reset();
	TestEqual(TEXT("A failed acquisition never clears another owner's state"), A->GetTagCount(State_Aiming), 1);
	First.Set(A, State_Aiming, true);
	First.Set(A, State_Aiming, true);
	Second.Set(A, State_Aiming, true);
	TestEqual(TEXT("Each independent owner contributes one count"), A->GetTagCount(State_Aiming), 3);
	First.Set(A, State_Aiming, false);
	First.Reset();
	First.Reset();
	TestEqual(TEXT("Repeated cleanup releases only the owner's contribution"), A->GetTagCount(State_Aiming), 2);
	Second.Reset();
	TestEqual(TEXT("Unrelated contribution survives both owners"), A->GetTagCount(State_Aiming), 1);
	First.Set(A, State_Attacking, true);
	First.Set(B, State_Attacking, true);
	TestEqual(TEXT("ASC rebinding releases the captured old ASC"), A->GetTagCount(State_Attacking), 0);
	TestEqual(TEXT("ASC rebinding acquires the new ASC"), B->GetTagCount(State_Attacking), 1);
	First.Reset();
	TestEqual(TEXT("Cleanup is applied to the acquisition ASC"), B->GetTagCount(State_Attacking), 0);
	A->RemoveLooseGameplayTag(State_Aiming);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FServerInputEventPolicyTest, "ArtisticSW.Lifecycle.Input.OutcomeIsolation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FServerInputEventPolicyTest::RunTest(const FString&)
{
	for (FGameplayTag Tag : TArray<FGameplayTag>{
	         Key_Default_Mouse_LeftClick, Key_Default_Mouse_RightClick, Key_Default_Mouse_LeftClick_Released,
	         Key_Default_Mouse_RightClick_Released, Key_Default_Mouse_WheelUp, Key_Default_Mouse_WheelDown})
		TestTrue(TEXT("Existing mouse input remains allowed"), ABasePlayer::IsAllowedRemoteInputEvent(Tag));
	for (FGameplayTag Tag :
	     TArray<FGameplayTag>{Interaction_Craft, State_Invulnerable, Event_Montage_FireArrow, FGameplayTag()})
		TestFalse(TEXT("Clients cannot forward gameplay outcomes as mouse input"),
		          ABasePlayer::IsAllowedRemoteInputEvent(Tag));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionDataLifetimeTest, "ArtisticSW.Interaction.TargetData.CacheLifetime",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInteractionDataLifetimeTest::RunTest(const FString&)
{
	PlayerLifeTests::FWorld Fixture;
	auto Life = Fixture.SpawnLife();
	UAbilitySystemComponent* ASC = Life.State->GetAbilitySystemComponent();
	const FGameplayAbilitySpecHandle Handle =
	    ASC->GiveAbility(FGameplayAbilitySpec(UGA_ThrowGrenade::StaticClass(), 1));
	FPredictionKey Key;
	FGameplayAbilityTargetDataHandle Submitted;
	Submitted.Add(new FGameplayAbilityTargetData_SingleTargetHit());
	// Simulate a packet that arrives before the server binds its ability callback.
	ASC->ServerSetReplicatedTargetData_Implementation(Handle, Key, Submitted, FGameplayTag(), Key);
	bool Received = false;
	const FDelegateHandle Subscription =
	    ASC->AbilityTargetDataSetDelegate(Handle, Key)
	        .AddLambda([&](const FGameplayAbilityTargetDataHandle& Borrowed, FGameplayTag) {
		        Received = true;
		        const FGameplayAbilityTargetDataHandle Stable =
		            UInteract::CopyAndConsumeTargetData(ASC, Handle, Key, Borrowed);
		        TestEqual(TEXT("Cached borrowed data was consumed"), Borrowed.Num(), 0);
		        TestEqual(TEXT("Processing retains the received hit after cache consumption"), Stable.Num(), 1);
		        TestNotNull(TEXT("Retained target data remains usable"), Stable.Get(0)->GetHitResult());
	        });
	ASC->CallReplicatedTargetDataDelegatesIfSet(Handle, Key);
	ASC->AbilityTargetDataSetDelegate(Handle, Key).Remove(Subscription);
	TestTrue(TEXT("Pending target data actually reached the callback"), Received);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionTargetValidationTest,
                                 "ArtisticSW.Interaction.TargetData.ReachAndLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInteractionTargetValidationTest::RunTest(const FString&)
{
	PlayerLifeTests::FWorld Fixture;
	auto Life = Fixture.SpawnLife();
	AActor* Target = Fixture.World->SpawnActor<AActor>();
	UInteractableComponent* Point = NewObject<UInteractableComponent>(Target);
	Target->SetRootComponent(Point);
	Point->RegisterComponent();
	Point->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Point->SetSphereRadius(50);
	Point->SetWorldLocation(Life.Player->GetActorLocation() + FVector(100, 0, 0));
	const UInteract* Ability = GetDefault<UInteract>();
	TestTrue(TEXT("Nearby authored interaction remains reachable"), Ability->ValidateServerTarget(Life.Player, Point));
	Point->SetWorldLocation(Life.Player->GetActorLocation() + FVector(10000, 0, 0));
	TestFalse(TEXT("A remote client cannot claim a distant component"),
	          Ability->ValidateServerTarget(Life.Player, Point));
	Point->SetWorldLocation(Life.Player->GetActorLocation());
	Target->SetActorHiddenInGame(true);
	TestFalse(TEXT("Hidden pooled actors cannot be interacted with"),
	          Ability->ValidateServerTarget(Life.Player, Point));
	Target->SetActorHiddenInGame(false);
	Point->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TestFalse(TEXT("Disabled interaction cannot be replayed"), Ability->ValidateServerTarget(Life.Player, Point));
	TestFalse(TEXT("Missing payload components are harmless"), Ability->ValidateServerTarget(Life.Player, nullptr));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryCountSafetyTest, "ArtisticSW.Inventory.Counts.Saturation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInventoryCountSafetyTest::RunTest(const FString&)
{
	UInventoryComponent* Inventory = NewObject<UInventoryComponent>();
	FInventoryTabPage Page;
	FInventorySlot Slot;
	Slot.ItemTag = Item_Id_Material_WeaponMaterial_Wood;
	Slot.Count = MAX_int32;
	Page.Slots.Add(Slot);
	Page.Slots.Add(Slot);
	Inventory->InventoryPages.Add(Page);
	TestEqual(TEXT("Large stacks cannot wrap an owned count negative"), Inventory->GetItemCount(Slot.ItemTag),
	          MAX_int32);
	Inventory->InventoryPages[0].Slots[0].Count = -5;
	Inventory->InventoryPages[0].Slots[1].Count = 3;
	TestEqual(TEXT("Malformed negative stacks do not cancel valid stock"), Inventory->GetItemCount(Slot.ItemTag), 3);
	TestEqual(TEXT("An empty tag has no inventory identity"), Inventory->GetItemCount(FGameplayTag()), 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrafterSessionSafetyTest, "ArtisticSW.Crafting.Legacy.SessionSafety",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrafterSessionSafetyTest::RunTest(const FString&)
{
	PlayerLifeTests::FWorld Fixture;
	auto Life = Fixture.SpawnLife();
	UCrafterComponent* Crafter = NewObject<UCrafterComponent>(Life.Player);
	Crafter->RegisterComponent();
	UAbilitySystemComponent* ASC = Life.State->GetAbilitySystemComponent();
	ASC->AddLooseGameplayTag(Class_Crafter);
	Crafter->SetupCrafterSystem();
	Crafter->SetupCrafterSystem();
	TestEqual(TEXT("Repeated setup cannot accumulate role tags"), ASC->GetTagCount(Class_Crafter), 2);
	Crafter->RemoveCrafterAbilities();
	TestEqual(TEXT("Role removal preserves another owner"), ASC->GetTagCount(Class_Crafter), 1);
	Crafter->ShowCraftingUI(nullptr);
	Crafter->ShowCraftingUI(Fixture.World->SpawnActor<AActor>());
	TestFalse(TEXT("Invalid worktable targets cannot create a session"), Crafter->CurrentInteractingTable.IsValid());
	AWorkTable* Table = Fixture.World->SpawnActor<AWorkTable>();
	Table->ItemTagToTestCraft = Item_Material_Submunition_Explosive;
	UInventoryComponent* Inventory = Life.Player->GetInventoryComponent();
	const int32 Before = Inventory->GetItemCount(Table->ItemTagToTestCraft);
	Crafter->Server_AttemptCrafting_Implementation(Table, 0.f, 30.f);
	TestEqual(TEXT("A caller-selected table is not an active server session"),
	          Inventory->GetItemCount(Table->ItemTagToTestCraft), Before);
	Crafter->CurrentInteractingTable = Table;
	for (const FVector2D Input :
	     {FVector2D(-1, 30), FVector2D(0, -1), FVector2D(0, std::numeric_limits<float>::infinity())})
		Crafter->Server_AttemptCrafting_Implementation(Table, Input.X, Input.Y);
	TestEqual(TEXT("Invalid timing input cannot grant output"), Inventory->GetItemCount(Table->ItemTagToTestCraft),
	          Before);
	AWorkTable* Other = Fixture.World->SpawnActor<AWorkTable>();
	Crafter->CurrentInteractingTable = Other;
	Crafter->Server_EndCrafting_Implementation(Table);
	TestTrue(TEXT("A delayed table A close cannot clear table B"), Crafter->CurrentInteractingTable.Get() == Other);
	Crafter->EndCrafting();
	Crafter->EndCrafting();
	TestFalse(TEXT("Repeated close clears the local session"), Crafter->CurrentInteractingTable.IsValid());
	ASC->RemoveLooseGameplayTag(Class_Crafter);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnipingServerValidationTest, "ArtisticSW.GAS.Sniping.ServerHitValidation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnipingServerValidationTest::RunTest(const FString&)
{
	PlayerLifeTests::FWorld Fixture;
	AActor* Source = Fixture.World->SpawnActor<AActor>();
	AActor* Target = Fixture.World->SpawnActor<AActor>();
	USphereComponent* Shape = NewObject<USphereComponent>(Target);
	Target->SetRootComponent(Shape);
	Shape->SetSphereRadius(35);
	Shape->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Shape->SetCollisionResponseToAllChannels(ECR_Block);
	Shape->RegisterComponent();
	Shape->SetWorldLocation(FVector(300, 0, 0));
	UGA_Sniping* Ability = NewObject<UGA_Sniping>();
	TestTrue(TEXT("Sniping participates in aim/swim cancellation"),
	         Ability->GetAssetTags().HasTagExact(GameplayAbility_Weapon_AimCycle));
	FHitResult Reported(Target, Shape, FVector(265, 0, 0), -FVector::ForwardVector), Verified;
	TestTrue(TEXT("Visible in-range hit is confirmed by a server trace"),
	         Ability->ValidateReportedHit(Source, Reported, Verified));
	AActor* Wall = Fixture.World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(10, 100, 100));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	Box->SetWorldLocation(FVector(100, 0, 0));
	TestFalse(TEXT("A client-reported hit cannot pass through a server-side wall"),
	          Ability->ValidateReportedHit(Source, Reported, Verified));
	Wall->Destroy();
	Ability->TraceDistance = 100;
	TestFalse(TEXT("Out-of-range damage is rejected"), Ability->ValidateReportedHit(Source, Reported, Verified));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGrenadeFailureIsolationTest, "ArtisticSW.GAS.Grenade.FailedActivationIsolation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGrenadeFailureIsolationTest::RunTest(const FString&)
{
	PlayerLifeTests::FWorld Fixture;
	auto Life = Fixture.SpawnLife();
	UAbilitySystemComponent* ASC = Life.State->GetAbilitySystemComponent();
	ASC->AddLooseGameplayTag(State_Aiming);
	ASC->AddLooseGameplayTag(State_Attacking);
	const FGameplayAbilitySpecHandle Handle =
	    ASC->GiveAbility(FGameplayAbilitySpec(UGA_ThrowGrenade::StaticClass(), 1));
	ASC->TryActivateAbility(Handle);
	TestFalse(TEXT("Missing equipped grenade ends the attempted activation"),
	          ASC->FindAbilitySpecFromHandle(Handle)->IsActive());
	TestEqual(TEXT("Failure cannot clear another aim owner"), ASC->GetTagCount(State_Aiming), 1);
	TestEqual(TEXT("Failure cannot clear another attack owner"), ASC->GetTagCount(State_Attacking), 1);
	ASC->RemoveLooseGameplayTag(State_Aiming);
	ASC->RemoveLooseGameplayTag(State_Attacking);
	const FStructProperty* Property =
	    FindFProperty<FStructProperty>(UGameplayAbility::StaticClass(), TEXT("ActivationOwnedTags"));
	for (const TCHAR* Path :
	     {TEXT("/Game/Blueprints/Player/Attacker/GA/GA_ThrowGrenade.GA_ThrowGrenade_C"),
	      TEXT("/Game/Blueprints/Player/Attacker/GA/GA_ThrowClusterGrenade.GA_ThrowClusterGrenade_C")})
	{
		UClass* Class = LoadClass<UGA_ThrowGrenade>(nullptr, Path);
		if (TestNotNull(TEXT("Production grenade Blueprint loads"), Class) && Property)
		{
			const auto* Ability = Class->GetDefaultObject<UGA_ThrowGrenade>();
			TestTrue(TEXT("Production grenade participates in swim/roll aim cancellation"),
			         Ability->GetAssetTags().HasTagExact(GameplayAbility_Weapon_AimCycle));
			TestTrue(TEXT("Production grenade participates in hit interruption"),
			         Ability->GetAssetTags().HasTagExact(GameplayAbility_InterruptibleByHit));
			const auto* Tags = Property->ContainerPtrToValuePtr<FGameplayTagContainer>(Class->GetDefaultObject());
			TestFalse(TEXT("Blueprints do not duplicate the native phase-owned aiming state"),
			          Tags->HasTagExact(State_Aiming));
		}
	}
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGrenadePhaseLifecycleTest, "ArtisticSW.GAS.Grenade.PhaseAndSingleCommit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGrenadePhaseLifecycleTest::RunTest(const FString&)
{
	PlayerLifeTests::FWorld Fixture;
	auto Life = Fixture.SpawnLife();
	UAbilitySystemComponent* ASC = Life.State->GetAbilitySystemComponent();
	FItemDefinition Definition;
	Definition.SpawnClass = AGrenadeProjectile::StaticClass();
	ABaseItem* Item = Fixture.World->SpawnActor<ABaseItem>();
	Item->MyDefinition = &Definition;
	Life.Player->EquippedItem = Item;
	ASC->AddLooseGameplayTag(State_Aiming);
	ASC->AddLooseGameplayTag(State_Attacking);
	const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(UGA_ThrowGrenade::StaticClass(), 1));
	ASC->FindAbilitySpecFromHandle(Handle)->InputPressed = true;
	TestTrue(TEXT("A held grenade activates"), ASC->TryActivateAbility(Handle));
	auto* Ability = Cast<UGA_ThrowGrenade>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
	if (!TestNotNull(TEXT("Instanced grenade exists"), Ability))
		return false;
	TestEqual(TEXT("Only one aim contribution is acquired while held"), ASC->GetTagCount(State_Aiming), 2);
	TestEqual(TEXT("Holding does not claim the release phase"), ASC->GetTagCount(State_Attacking), 1);
	const FGameplayTagContainer CancelTags(GameplayAbility_Weapon_AimCycle);
	ASC->CancelAbilities(&CancelTags);
	TestFalse(TEXT("Swimming/roll aim cancellation stops the held grenade"), Ability->IsActive());
	TestEqual(TEXT("Interrupted aim preserves another owner's count"), ASC->GetTagCount(State_Aiming), 1);
	TestTrue(TEXT("An interrupted grenade is retained"), IsValid(Life.Player->EquippedItem));
	ASC->FindAbilitySpecFromHandle(Handle)->InputPressed = true;
	TestTrue(TEXT("The retained grenade can be aimed again"), ASC->TryActivateAbility(Handle));
	// A notify before release must not create a projectile.
	Ability->OnThrowEventReceived({});
	int32 Before = 0;
	for (TActorIterator<AGrenadeProjectile> It(Fixture.World.Get()); It; ++It)
		++Before;
	TestEqual(TEXT("Early animation notify is harmless"), Before, 0);
	Ability->OnInputReleased(.2f);
	int32 After = 0;
	for (TActorIterator<AGrenadeProjectile> It(Fixture.World.Get()); It; ++It)
		++After;
	TestEqual(TEXT("Release without a montage creates one server projectile"), After, 1);
	TestFalse(TEXT("Montage-free release finishes"), Ability->IsActive());
	TestEqual(TEXT("Release restores the foreign aim count"), ASC->GetTagCount(State_Aiming), 1);
	TestEqual(TEXT("End restores the foreign attack count"), ASC->GetTagCount(State_Attacking), 1);
	Ability->OnInputReleased(.2f);
	Ability->OnThrowEventReceived({});
	int32 Repeated = 0;
	for (TActorIterator<AGrenadeProjectile> It(Fixture.World.Get()); It; ++It)
		++Repeated;
	TestEqual(TEXT("Duplicate release and late notify cannot spawn another grenade"), Repeated, After);
	ASC->RemoveLooseGameplayTag(State_Aiming);
	ASC->RemoveLooseGameplayTag(State_Attacking);
	return !HasAnyErrors();
}
#endif
