#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseCharacter.h"
#include "BaseGameplayAbility.h"
#include "BaseGameplayTags.h"
#include "Components/StatusComponent.h"
#include "Components/BaseHealthComponent.h"
#include "Effects/StatusGameplayEffect.h"
#include "Effects/AreaSlowGameplayEffect.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "StatusEffectLibrary.h"
#include "Abilities/BaseDeathGameplayAbility.h"

namespace StatusTests
{
	struct FWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("StatusTests"));
		FWorld() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
		~FWorld() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
		ABaseCharacter* Character()
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			auto* Actor = World->SpawnActor<ABaseCharacter>(Params);
			Actor->AbilitySystemComponent = NewObject<UAbilitySystemComponent>(Actor);
			Actor->AbilitySystemComponent->RegisterComponent();
			Actor->AbilitySystemComponent->InitAbilityActorInfo(Actor, Actor);
			Actor->AbilitySystemComponent->AddAttributeSetSubobject(NewObject<UBaseAttributeSet>(Actor));
			Actor->StatusComponent->InitializeWithAbilitySystem(Actor->AbilitySystemComponent);
			return Actor;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStatusFixedLifetimeTest, "ArtisticSW.GAS.Status.FixedLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStatusFixedLifetimeTest::RunTest(const FString& Parameters)
{
	// Unrelated malformed crafting data is loaded by every transient world.
	AddExpectedError(TEXT("QuestItem (has an invalid ResultItemTag|contains an invalid ingredient)"), EAutomationExpectedErrorFlags::Contains, 0);
	StatusTests::FWorld Test;
	ABaseCharacter* Target = Test.Character();
	UAbilitySystemComponent* ASC = Target->AbilitySystemComponent;
	const auto ActiveAction = ASC->GiveAbility(FGameplayAbilitySpec(UBaseGameplayAbility::StaticClass(), 1));
	TestTrue(TEXT("Action can start"), ASC->TryActivateAbility(ActiveAction));
	const auto First = Target->StatusComponent->ApplyStatus(UStunGameplayEffect::StaticClass(), ASC, {});
	if (!TestTrue(TEXT("Stun is applied"), First.IsValid())) return false;
	TestTrue(TEXT("Stun blocks movement"), Target->IsMoveInputIgnored());
	TestFalse(TEXT("Running action is canceled"), ASC->FindAbilitySpecFromHandle(ActiveAction)->IsActive());
	TestFalse(TEXT("New action is blocked"), ASC->TryActivateAbility(ActiveAction));
	TestTrue(TEXT("Death bypasses control block"), GetDefault<UBaseDeathGameplayAbility>()->IsAllowedDuringControlBlock());
	ASC->ModifyActiveEffectStartTime(First, -1.f);
	const float OriginalStart = ASC->GetActiveGameplayEffect(First)->StartWorldTime;
	const auto Retry = Target->StatusComponent->ApplyStatus(UStunGameplayEffect::StaticClass(), ASC, {});
	TestFalse(TEXT("Stun retry is rejected"), Retry.IsValid());
	TestEqual(TEXT("No timer reset"), ASC->GetActiveGameplayEffect(First)->StartWorldTime, OriginalStart);
	// Advance real world/GE timers beyond the original duration.
	for (int i = 0; i < 25; ++i) { ++GFrameCounter; Test.World->Tick(LEVELTICK_All, 0.1f); }
	TestFalse(TEXT("Stun expires on its original clock"), ASC->HasMatchingGameplayTag(State_Status_Stun));
	TestFalse(TEXT("Movement restored"), Target->IsMoveInputIgnored());
	TestTrue(TEXT("Action can restart"), ASC->TryActivateAbility(ActiveAction));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStatusReceptionTest, "ArtisticSW.GAS.Status.ReceptionAndCoexistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStatusReceptionTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("QuestItem (has an invalid ResultItemTag|contains an invalid ingredient)"), EAutomationExpectedErrorFlags::Contains, 0);
	StatusTests::FWorld Test;
	ABaseCharacter* Target = Test.Character();
	UAbilitySystemComponent* ASC = Target->AbilitySystemComponent;
	AActor* ShipLikeActor = Test.World->SpawnActor<AActor>();
	auto* OtherASC = NewObject<UAbilitySystemComponent>(ShipLikeActor);
	OtherASC->RegisterComponent();
	OtherASC->InitAbilityActorInfo(ShipLikeActor, ShipLikeActor);
	OtherASC->AddAttributeSetSubobject(NewObject<UBaseAttributeSet>(ShipLikeActor));
	auto Poison = ASC->MakeOutgoingSpec(UPoisonStatusGameplayEffect::StaticClass(), 1.f, ASC->MakeEffectContext());
	TestFalse(TEXT("Direct GE rejects ASC-only object"), OtherASC->ApplyGameplayEffectSpecToSelf(*Poison.Data).IsValid());
	TestTrue(TEXT("Poison accepted"), Target->StatusComponent->ApplyStatus(UPoisonStatusGameplayEffect::StaticClass(), ASC, {}).IsValid());
	TestTrue(TEXT("Burn coexists with poison"), Target->StatusComponent->ApplyStatus(UBurnStatusGameplayEffect::StaticClass(), ASC, {}).IsValid());
	TestFalse(TEXT("Direct ASC reapplication also rejected"), ASC->ApplyGameplayEffectSpecToSelf(*Poison.Data).IsValid());
	for (int i = 0; i < 12; ++i) { ++GFrameCounter; Test.World->Tick(LEVELTICK_All, 0.1f); }
	TestTrue(TEXT("Periodic effects apply actual damage"), ASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()) < 100.f);
	Target->StatusComponent->ClearStatuses();
	Target->StatusComponent->ImmuneStatuses.AddTag(State_Status_Stun);
	TestFalse(TEXT("Receiver-specific immunity"), Target->StatusComponent->ApplyStatus(UStunGameplayEffect::StaticClass(), ASC, {}).IsValid());
	Target->StatusComponent->ImmuneStatuses.Reset();
	TestTrue(TEXT("Stun before death"), Target->StatusComponent->ApplyStatus(UStunGameplayEffect::StaticClass(), ASC, {}).IsValid());
	ASC->AddLooseGameplayTag(State_Dead);
	TestFalse(TEXT("Death removes status GE"), ASC->HasMatchingGameplayTag(State_Status));
	TestFalse(TEXT("Dead actors reject status"), Target->StatusComponent->ApplyStatus(UBurnStatusGameplayEffect::StaticClass(), ASC, {}).IsValid());
	return !HasAnyErrors();
}
#endif
