#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"
#include "WaterBombCannonball.h"
#include "WaterBombEffects.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWaterBombEffectConfigurationTest,
	"ArtisticSW.WaterBomb.EffectConfiguration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWaterBombEffectConfigurationTest::RunTest(const FString& Parameters)
{
	const UWaterBombAttackSpeedGameplayEffect* SlowEffect = GetDefault<UWaterBombAttackSpeedGameplayEffect>();
	TestEqual(TEXT("Slow GE has duration"), SlowEffect->DurationPolicy, EGameplayEffectDurationType::HasDuration);
	TestEqual(TEXT("Slow GE has one modifier"), SlowEffect->Modifiers.Num(), 1);
	if (SlowEffect->Modifiers.Num() == 1)
	{
		TestTrue(TEXT("Slow GE targets AttackSpeedMultiplier"),
			SlowEffect->Modifiers[0].Attribute == UBaseAttributeSet::GetAttackSpeedMultiplierAttribute());
		TestEqual(TEXT("Slow GE uses multiplicative operation"),
			SlowEffect->Modifiers[0].ModifierOp,
			EGameplayModOp::Multiplicitive);
	}

	const AWaterBombCannonball* Projectile = GetDefault<AWaterBombCannonball>();
	TestEqual(TEXT("Default water-bomb duration"), Projectile->GetEffectDurationSeconds(), 5.0f);
	TestEqual(TEXT("Default water-bomb attack speed multiplier"), Projectile->GetAttackSpeedMultiplier(), 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWaterBombGameplayEffectApplicationTest,
	"ArtisticSW.WaterBomb.GameplayEffectApplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWaterBombGameplayEffectApplicationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("WaterBombEffectTestWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	AActor* OwnerActor = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("ASC owner actor is spawned"), OwnerActor))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}

	UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(OwnerActor, TEXT("WaterBombTestASC"));
	OwnerActor->AddInstanceComponent(ASC);
	ASC->RegisterComponent();
	// Runtime AttributeSet은 ASC가 아니라 ASC를 소유한 Actor의 subobject입니다.
	UBaseAttributeSet* Attributes = NewObject<UBaseAttributeSet>(OwnerActor, TEXT("WaterBombTestAttributes"));
	ASC->AddAttributeSetSubobject(Attributes);
	ASC->InitAbilityActorInfo(OwnerActor, OwnerActor);

	TestEqual(TEXT("Attack speed starts at normal speed"),
		ASC->GetNumericAttribute(UBaseAttributeSet::GetAttackSpeedMultiplierAttribute()),
		1.0f);

	FGameplayEffectSpecHandle SlowSpec = ASC->MakeOutgoingSpec(
		UWaterBombAttackSpeedGameplayEffect::StaticClass(),
		1.0f,
		ASC->MakeEffectContext());
	TestTrue(TEXT("Slow GE spec is created"), SlowSpec.IsValid());
	if (!SlowSpec.IsValid())
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}

	SlowSpec.Data->SetDuration(5.0f, true);
	SlowSpec.Data->SetSetByCallerMagnitude(Data_Effect_AttackSpeedMultiplier, 0.5f);
	SlowSpec.Data->DynamicGrantedTags.AddTag(State_Debuff_WaterBomb);
	const FActiveGameplayEffectHandle SlowHandle = ASC->ApplyGameplayEffectSpecToSelf(*SlowSpec.Data);

	TestTrue(TEXT("Slow GE is active"), SlowHandle.IsValid());
	TestEqual(TEXT("Slow GE halves AttackSpeedMultiplier"),
		ASC->GetNumericAttribute(UBaseAttributeSet::GetAttackSpeedMultiplierAttribute()),
		0.5f);
	TestTrue(TEXT("Slow GE grants water-bomb debuff tag"), ASC->HasMatchingGameplayTag(State_Debuff_WaterBomb));

	ASC->RemoveActiveGameplayEffect(SlowHandle);
	TestEqual(TEXT("Removing GE restores normal attack speed"),
		ASC->GetNumericAttribute(UBaseAttributeSet::GetAttackSpeedMultiplierAttribute()),
		1.0f);
	TestFalse(TEXT("Removing GE clears water-bomb debuff tag"), ASC->HasMatchingGameplayTag(State_Debuff_WaterBomb));

	FGameplayEffectSpecHandle DisableSpec = ASC->MakeOutgoingSpec(
		UWaterBombCannonDisableGameplayEffect::StaticClass(),
		1.0f,
		ASC->MakeEffectContext());
	TestTrue(TEXT("Cannon-disable GE spec is created"), DisableSpec.IsValid());
	if (DisableSpec.IsValid())
	{
		DisableSpec.Data->SetDuration(5.0f, true);
		DisableSpec.Data->DynamicGrantedTags.AddTag(State_Ship_CannonDisabled);
		const FActiveGameplayEffectHandle DisableHandle = ASC->ApplyGameplayEffectSpecToSelf(*DisableSpec.Data);
		TestTrue(TEXT("Cannon-disable GE is active"), DisableHandle.IsValid());
		TestTrue(TEXT("Cannon-disable GE grants fire-blocking tag"), ASC->HasMatchingGameplayTag(State_Ship_CannonDisabled));
		ASC->RemoveActiveGameplayEffect(DisableHandle);
		TestFalse(TEXT("Removing cannon-disable GE clears tag"), ASC->HasMatchingGameplayTag(State_Ship_CannonDisabled));
	}

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
