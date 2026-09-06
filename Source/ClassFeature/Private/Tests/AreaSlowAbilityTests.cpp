#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseCharacter.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "BasePlayerState.h"
#include "Effects/AreaSlowGameplayEffect.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"
#include "Skills/Abilities/GA_PlayerAreaSlow.h"
#include "Skills/AreaSlowDecalActors.h"
#include "Skills/AreaSlowSkillDataAsset.h"
#include "Skills/PlayerSkillComponent.h"

namespace AreaSlowAbilityTests
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("AreaSlowPolicyTestWorld"));
			if (World)
			{
				FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
				WorldContext.SetCurrentWorld(World);
			}
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}
	};

	void ConfigureSlowableTarget(ABaseCharacter* Target)
	{
		Target->AbilitySystemComponent = NewObject<UAbilitySystemComponent>(Target);
		Target->AbilitySystemComponent->RegisterComponent();
		Target->AbilitySystemComponent->InitAbilityActorInfo(Target, Target);
		Target->AbilitySystemComponent->AddAttributeSetSubobject(
			NewObject<UBaseAttributeSet>(Target));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAreaSlowConfigurationTest,
	"ArtisticSW.AreaSlow.Configuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAreaSlowConfigurationTest::RunTest(const FString& Parameters)
{
	const UGA_PlayerAreaSlow* Ability = GetDefault<UGA_PlayerAreaSlow>();
	TestEqual(
		TEXT("Area Slow keeps one ability instance per player"),
		Ability->GetInstancingPolicy(),
		EGameplayAbilityInstancingPolicy::InstancedPerActor);
	TestEqual(
		TEXT("Area Slow uses GAS local prediction"),
		Ability->GetNetExecutionPolicy(),
		EGameplayAbilityNetExecutionPolicy::LocalPredicted);
	TestTrue(
		TEXT("Area Slow owns its exact skill tag"),
		Ability->GetAssetTags().HasTagExact(GameplayAbility_Skill_AreaSlow));

	const ABasePlayer* Player = GetDefault<ABasePlayer>();
	TestTrue(TEXT("Player grants Area Slow input by default"), Player->bEnableAreaSlowSkillInput);
	TestEqual(
		TEXT("Player defaults to the native Area Slow ability"),
		Player->AreaSlowAbilityClass.Get(),
		UGA_PlayerAreaSlow::StaticClass());
	const UPlayerSkillComponent* SkillDefaults = GetDefault<UPlayerSkillComponent>();
	TestEqual(
		TEXT("Area Slow is registered with the player skill inventory policy"),
		SkillDefaults->GetUsageMaterialTag(GameplayAbility_Skill_AreaSlow),
		Item_Id_Material_SkillMaterial_RareSkill.GetTag());

	const AAreaSlowTargetingDecal* Preview = GetDefault<AAreaSlowTargetingDecal>();
	const AAreaSlowConfirmedDecal* Confirmed = GetDefault<AAreaSlowConfirmedDecal>();
	TestFalse(TEXT("Hold preview never replicates"), Preview->GetIsReplicated());
	TestTrue(TEXT("Confirmed visual replicates to clients"), Confirmed->GetIsReplicated());
	TestTrue(TEXT("Confirmed visual is always network relevant during its short life"), Confirmed->bAlwaysRelevant);
	TestFalse(TEXT("Confirmed visual does not perform a repeated range search"), Confirmed->PrimaryActorTick.bCanEverTick);

	const UAreaSlowGameplayEffect* Effect = GetDefault<UAreaSlowGameplayEffect>();
	TestEqual(
		TEXT("Slow effect is duration based"),
		Effect->DurationPolicy,
		EGameplayEffectDurationType::HasDuration);
	TestEqual(TEXT("Repeated casts keep one target-owned stack"), Effect->GetStackLimitCount(), 1);
	TestEqual(TEXT("Slow effect owns movement and attack modifiers"), Effect->Modifiers.Num(), 2);
	if (Effect->Modifiers.Num() == 2)
	{
		TestTrue(
			TEXT("Slow effect modifies the shared movement multiplier"),
			Effect->Modifiers[0].Attribute == UBaseAttributeSet::GetMoveSpeedMultiplierAttribute());
		TestEqual(
			TEXT("Slow effect multiplies rather than overwriting movement speed"),
			Effect->Modifiers[0].ModifierOp,
			EGameplayModOp::Multiplicitive);
		TestTrue(
			TEXT("Slow effect modifies the shared attack multiplier"),
			Effect->Modifiers[1].Attribute == UBaseAttributeSet::GetAttackSpeedMultiplierAttribute());
		TestEqual(
			TEXT("Slow effect multiplies rather than overwriting attack speed"),
			Effect->Modifiers[1].ModifierOp,
			EGameplayModOp::Multiplicitive);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAreaSlowInputLifecycleTest,
	"ArtisticSW.AreaSlow.InputLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAreaSlowInputLifecycleTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("QuestItem"), EAutomationExpectedErrorFlags::Contains, 3);
	AreaSlowAbilityTests::FScopedTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient input world is created"), TestWorld.World))
	{
		return false;
	}

	UGA_PlayerAreaSlow* AbilityDefaults = GetMutableDefault<UGA_PlayerAreaSlow>();
	UAreaSlowSkillDataAsset* PreviousSkillData = AbilityDefaults->SkillData;
	UAreaSlowSkillDataAsset* TestSkillData = NewObject<UAreaSlowSkillDataAsset>();
	AbilityDefaults->SkillData = TestSkillData;
	ON_SCOPE_EXIT
	{
		AbilityDefaults->SkillData = PreviousSkillData;
	};

	ABasePlayerState* PlayerState = TestWorld.World->SpawnActor<ABasePlayerState>();
	APlayerController* PlayerController = TestWorld.World->SpawnActor<APlayerController>();
	ABasePlayer* Player = TestWorld.World->SpawnActor<ABasePlayer>();
	if (!TestNotNull(TEXT("PlayerState is spawned"), PlayerState)
		|| !TestNotNull(TEXT("PlayerController is spawned"), PlayerController)
		|| !TestNotNull(TEXT("Player is spawned"), Player))
	{
		return false;
	}

	Player->bBypassSkillRequirementsForTesting = true;
	Player->SetPlayerState(PlayerState);
	PlayerController->Possess(Player);
	UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("Player ASC is initialized"), ASC))
	{
		return false;
	}
	FGameplayAbilitySpec* AreaSlowSpec = ASC->FindAbilitySpecFromClass(UGA_PlayerAreaSlow::StaticClass());
	UGA_PlayerAreaSlow* AreaSlowInstance = AreaSlowSpec
		? Cast<UGA_PlayerAreaSlow>(AreaSlowSpec->GetPrimaryInstance())
		: nullptr;
	if (!TestNotNull(TEXT("Area Slow per-player ability instance exists"), AreaSlowInstance))
	{
		return false;
	}
	AreaSlowInstance->SkillData = TestSkillData;

	Player->OnAreaSlowSkillPressed();
	TestTrue(
		TEXT("Holding the dedicated skill input enters Area Slow aiming"),
		ASC->HasMatchingGameplayTag(GameplayAbility_Skill_AreaSlow));
	// This headless fixture has no ULocalPlayer, so the owner-only preview branch
	// is covered by its non-replicating class contract in Configuration instead.

	Player->OnMouseInputPressed(Key_Default_Mouse_RightClick);
	TestFalse(
		TEXT("Right click cancels Area Slow aiming"),
		ASC->HasMatchingGameplayTag(GameplayAbility_Skill_AreaSlow));

	Player->OnAreaSlowSkillPressed();
	TestTrue(
		TEXT("Area Slow can re-enter aiming after cancellation"),
		ASC->HasMatchingGameplayTag(GameplayAbility_Skill_AreaSlow));
	Player->OnMouseInputPressed(Key_Default_Mouse_LeftClick);
	TestFalse(
		TEXT("Left click confirms and ends the one-shot ability"),
		ASC->HasMatchingGameplayTag(GameplayAbility_Skill_AreaSlow));

	int32 ConfirmedCount = 0;
	for (TActorIterator<AAreaSlowConfirmedDecal> It(TestWorld.World); It; ++It)
	{
		++ConfirmedCount;
		TestTrue(TEXT("Confirmed visual has a bounded server lifespan"), It->GetLifeSpan() > 0.0f);
	}
	TestEqual(TEXT("One confirmation spawns one replicated visual"), ConfirmedCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAreaSlowRangeAndPolicyTest,
	"ArtisticSW.AreaSlow.RangeAndTargetPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAreaSlowRangeAndPolicyTest::RunTest(const FString& Parameters)
{
	// Creating a project game world initializes the existing ItemSubsystem,
	// whose current QuestItem authoring emits these unrelated known errors.
	AddExpectedError(TEXT("QuestItem"), EAutomationExpectedErrorFlags::Contains, 3);
	AreaSlowAbilityTests::FScopedTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient policy world is created"), TestWorld.World))
	{
		return false;
	}

	UAreaSlowSkillDataAsset* Data = NewObject<UAreaSlowSkillDataAsset>();
	FString FailureReason;
	TestTrue(TEXT("Native Data Asset defaults are runtime-valid"), Data->IsRuntimeConfigValid(&FailureReason));
	if (!FailureReason.IsEmpty())
	{
		AddError(FString::Printf(TEXT("Unexpected validation failure: %s"), *FailureReason));
	}

	const FAreaSlowRange Range = UAreaSlowSkillDataAsset::BuildRangeForTransform(
		FTransform(FRotator(12.0f, 90.0f, 8.0f), FVector(100.0f, 200.0f, 30.0f)),
		100.0f,
		800.0f,
		500.0f,
		300.0f,
		20.0f);
	TestTrue(
		TEXT("Snapshot center is placed once in front of player yaw"),
		Range.Center.Equals(FVector(100.0f, 700.0f, 50.0f), 0.1f));
	TestTrue(
		TEXT("Designer dimensions are converted to oriented-box half extents"),
		Range.BoxExtent.Equals(FVector(400.0f, 250.0f, 150.0f), 0.1f));
	TestTrue(TEXT("Pitch is ignored for a ground range"), FMath::IsNearlyZero(Range.Rotation.Pitch));
	TestTrue(TEXT("Player yaw is retained"), FMath::IsNearlyEqual(Range.Rotation.Yaw, 90.0f));
	TestTrue(TEXT("Roll is ignored for a ground range"), FMath::IsNearlyZero(Range.Rotation.Roll));

	ABasePlayer* SourcePlayer = TestWorld.World->SpawnActor<ABasePlayer>();
	ABaseCharacter* Target = TestWorld.World->SpawnActor<ABaseCharacter>();
	if (!TestNotNull(TEXT("Source player is spawned"), SourcePlayer)
		|| !TestNotNull(TEXT("Candidate target is spawned"), Target))
	{
		return false;
	}
	AreaSlowAbilityTests::ConfigureSlowableTarget(Target);

	TestFalse(
		TEXT("An untagged actor is excluded even when it supports the movement attribute"),
		UGA_PlayerAreaSlow::IsEligibleTarget(SourcePlayer, Target, Data));

	Target->AbilitySystemComponent->AddLooseGameplayTag(Targetable_Skill_AreaSlow);
	TestFalse(
		TEXT("An opted-in actor without a movement adapter capability is excluded"),
		UGA_PlayerAreaSlow::IsEligibleTarget(SourcePlayer, Target, Data));
	Target->AbilitySystemComponent->AddLooseGameplayTag(Capability_Effect_MoveSpeedMultiplier);
	TestFalse(
		TEXT("An opted-in actor without attack-speed support is excluded"),
		UGA_PlayerAreaSlow::IsEligibleTarget(SourcePlayer, Target, Data));
	Target->AbilitySystemComponent->AddLooseGameplayTag(Capability_Effect_AttackSpeedMultiplier);
	TestTrue(
		TEXT("An explicitly opted-in actor supporting both affected attributes is eligible"),
		UGA_PlayerAreaSlow::IsEligibleTarget(SourcePlayer, Target, Data));

	FGameplayEffectSpecHandle SlowSpec = Target->AbilitySystemComponent->MakeOutgoingSpec(
		UAreaSlowGameplayEffect::StaticClass(),
		1.0f,
		Target->AbilitySystemComponent->MakeEffectContext());
	if (TestTrue(TEXT("A duration slow spec is created"), SlowSpec.IsValid() && SlowSpec.Data.IsValid()))
	{
		SlowSpec.Data->SetDuration(0.1f, true);
		SlowSpec.Data->SetSetByCallerMagnitude(Data_Effect_MoveSpeedMultiplier, 0.5f);
		SlowSpec.Data->SetSetByCallerMagnitude(Data_Effect_AttackSpeedMultiplier, 0.6f);
		SlowSpec.Data->DynamicGrantedTags.AddTag(State_Debuff_Slow);
		TestEqual(TEXT("Each target spec carries its personal duration"), SlowSpec.Data->GetDuration(), 0.1f);
		const FActiveGameplayEffectHandle SlowHandle =
			Target->AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SlowSpec.Data.Get());
		TestTrue(TEXT("Eligible target accepts the slow effect"), SlowHandle.IsValid());
		TestEqual(
			TEXT("Slow tag lifetime applies the configured movement multiplier"),
			Target->AbilitySystemComponent->GetNumericAttribute(
				UBaseAttributeSet::GetMoveSpeedMultiplierAttribute()),
			0.5f);
		TestEqual(
			TEXT("Slow tag lifetime applies the independently configured attack multiplier"),
			Target->AbilitySystemComponent->GetNumericAttribute(
				UBaseAttributeSet::GetAttackSpeedMultiplierAttribute()),
			0.6f);
		TestTrue(
			TEXT("Target owns State.Debuff.Slow for the effect duration"),
			Target->AbilitySystemComponent->HasMatchingGameplayTag(State_Debuff_Slow));

		// The full engine timer path owns natural expiry. Removing the same active
		// handle exercises the exact GAS teardown path used when that timer fires.
		Target->AbilitySystemComponent->RemoveActiveGameplayEffect(SlowHandle);
		TestEqual(
			TEXT("Movement multiplier restores when the duration effect is removed"),
			Target->AbilitySystemComponent->GetNumericAttribute(
				UBaseAttributeSet::GetMoveSpeedMultiplierAttribute()),
			1.0f);
		TestEqual(
			TEXT("Attack multiplier restores with the same duration effect"),
			Target->AbilitySystemComponent->GetNumericAttribute(
				UBaseAttributeSet::GetAttackSpeedMultiplierAttribute()),
			1.0f);
		TestFalse(
			TEXT("Slow state tag is removed with the duration effect"),
			Target->AbilitySystemComponent->HasMatchingGameplayTag(State_Debuff_Slow));
	}

	Target->AbilitySystemComponent->AddLooseGameplayTag(Immunity_Debuff_Slow);
	TestFalse(
		TEXT("Data Asset blocked tags take precedence over opt-in"),
		UGA_PlayerAreaSlow::IsEligibleTarget(SourcePlayer, Target, Data));
	Target->AbilitySystemComponent->RemoveLooseGameplayTag(Immunity_Debuff_Slow);

	ABasePlayer* OtherPlayer = TestWorld.World->SpawnActor<ABasePlayer>();
	TestFalse(
		TEXT("Every player character is excluded as an invariant"),
		UGA_PlayerAreaSlow::IsEligibleTarget(SourcePlayer, OtherPlayer, Data));

	AActor* UnsupportedActor = TestWorld.World->SpawnActor<AActor>();
	TestFalse(
		TEXT("A tagged query can never apply a GAS effect to an unsupported actor"),
		UGA_PlayerAreaSlow::IsEligibleTarget(SourcePlayer, UnsupportedActor, Data));

	return true;
}

#endif
