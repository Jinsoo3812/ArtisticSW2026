#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "BossAI/ShipBossEnemy.h"
#include "Components/BaseHealthComponent.h"
#include "Components/StatusComponent.h"
#include "GAS/Ability/Boss/BossStunEffects.h"
#include "GASDamageInstantGameplayEffect.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "UObject/Script.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossStatusTriggersTest, "ArtisticSW.Enemy.Boss.StatusTriggers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBossStatusTriggersTest::RunTest(const FString& Parameters)
{
	// This isolated world has no BeginPlay; allow the actor's dynamic health delegate to execute.
	FEditorScriptExecutionGuard ScriptExecutionGuard;
	AddExpectedError(TEXT("QuestItem (has an invalid ResultItemTag|contains an invalid ingredient)"), EAutomationExpectedErrorFlags::Contains, 0);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("BossStatusTests"));
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShipBossEnemy* Boss = World->SpawnActor<AShipBossEnemy>(Params);
	UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent();
	ASC->InitAbilityActorInfo(Boss, Boss);
	auto* Attributes = Cast<UBaseAttributeSet>(Boss->GetDefaultSubobjectByName(TEXT("BasicAttributeSet")));
	if (!TestNotNull(TEXT("Boss native attributes exist"), Attributes)) return false;
	ASC->AddAttributeSetSubobject(Attributes);
	ASC->SetNumericAttributeBase(UBaseAttributeSet::GetMaxHealthAttribute(), 100.f);
	ASC->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), 100.f);
	UBaseHealthComponent* Health = Boss->GetHealthComponent();
	Health->InitializeWithAbilitySystem(ASC);
	Health->OnConfirmedDamage.AddUObject(Boss, &AShipBossEnemy::HandleConfirmedDamage);
	Health->OnHealthChanged.AddUniqueDynamic(Boss, &AShipBossEnemy::HandleStunHealthChanged);
	auto Damage = [&](FName Bone, float Amount)
	{
		FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		FHitResult Hit;
		Hit.BoneName = Bone;
		Context.AddHitResult(Hit, true);
		auto Spec = ASC->MakeOutgoingSpec(UGASDamageInstantGameplayEffect::StaticClass(), 1.f, Context);
		Spec.Data->SetSetByCallerMagnitude(Data_Damage, Amount);
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	};
	auto Duration = [&]()
	{
		const auto Values = ASC->GetActiveEffectsDuration(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
			FGameplayTagContainer(State_Status_Stun)));
		return Values.Num() == 1 ? Values[0] : -1.f;
	};
	Damage(TEXT("head"), 1.f);
	TestFalse(TEXT("Idle head hit does not stun"), Boss->StatusComponent->HasStatus(State_Status_Stun));
	ASC->AddLooseGameplayTag(State_Attacking);
	Damage(TEXT("spine_01"), 1.f);
	TestFalse(TEXT("Body hit during attack does not stun"), Boss->StatusComponent->HasStatus(State_Status_Stun));
	Damage(TEXT("head"), 1.f);
	TestTrue(TEXT("Attacking head hit stuns without a normal HitReaction GA"), Boss->StatusComponent->HasStatus(State_Status_Stun));
	TestEqual(TEXT("Head hit selects two second GE"), Duration(), 2.f);
	Damage(TEXT("head"), 1.f);
	TestEqual(TEXT("Repeated head hit keeps original duration"), Duration(), 2.f);
	TestFalse(TEXT("Different Stun GE cannot replace head-hit Stun"), Boss->StatusComponent->ApplyStatus(
		UBossHealthThresholdStunEffect::StaticClass(), ASC, {}).IsValid());
	Boss->StatusComponent->ClearStatuses();
	ASC->RemoveLooseGameplayTag(State_Attacking);
	Damage(TEXT("spine_01"), 50.f);
	TestEqual(TEXT("HP crossing selects three second GE without attacking"), Duration(), 3.f);
	Boss->StatusComponent->ClearStatuses();
	ASC->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), 80.f);
	Damage(TEXT("spine_01"), 40.f);
	TestFalse(TEXT("Threshold fires once even after healing"), Boss->StatusComponent->HasStatus(State_Status_Stun));
	ASC->AddLooseGameplayTag(State_Attacking);
	Damage(TEXT("head"), 500.f);
	TestFalse(TEXT("Lethal head hit only causes death"), Boss->StatusComponent->HasStatus(State_Status_Stun));
	return !HasAnyErrors();
}
#endif
