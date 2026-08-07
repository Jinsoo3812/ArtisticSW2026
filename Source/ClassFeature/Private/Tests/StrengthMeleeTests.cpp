#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseCharacter.h"
#include "BaseGameplayTags.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GASCombatLibrary.h"
#include "GASDamageInstantGameplayEffect.h"
#include "Item/Weapons/SwordItem.h"
#include "WeaponFeedback/WeaponFeedbackComponent.h"

namespace StrengthMeleeTests
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("StrengthMeleeTestWorld"));
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

	UAbilitySystemComponent* AddAbilitySystem(ABaseCharacter* Character, UBaseAttributeSet*& OutAttributes)
	{
		UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Character);
		ASC->RegisterComponent();
		ASC->InitAbilityActorInfo(Character, Character);
		Character->AbilitySystemComponent = ASC;
		OutAttributes = NewObject<UBaseAttributeSet>(Character);
		ASC->AddAttributeSetSubobject(OutAttributes);
		return ASC;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStrengthMeleePayloadTest,
	"ArtisticSW.GAS.Strength.MeleePayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrengthMeleePayloadTest::RunTest(const FString& Parameters)
{
	StrengthMeleeTests::FScopedTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	ABaseCharacter* SourceCharacter = TestWorld.World->SpawnActor<ABaseCharacter>();
	ABaseCharacter* TargetCharacter = TestWorld.World->SpawnActor<ABaseCharacter>(
		ABaseCharacter::StaticClass(), FVector(5000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	ASwordItem* Sword = TestWorld.World->SpawnActor<ASwordItem>();
	if (!TestNotNull(TEXT("Source character is spawned"), SourceCharacter)
		|| !TestNotNull(TEXT("Target character is spawned"), TargetCharacter)
		|| !TestNotNull(TEXT("Sword is spawned"), Sword)
		|| !TestNotNull(TEXT("Sword owns the shared weapon feedback component"), Sword ? Sword->GetWeaponFeedbackComponent() : nullptr))
	{
		return false;
	}

	TestFalse(
		TEXT("Unconfigured feedback safely skips a swing sound"),
		Sword->GetWeaponFeedbackComponent()->PlaySwingSound());
	TestFalse(
		TEXT("Unconfigured feedback safely skips a trail"),
		Sword->GetWeaponFeedbackComponent()->BeginWeaponTrail());

	UBaseAttributeSet* SourceAttributes = nullptr;
	UAbilitySystemComponent* SourceASC = StrengthMeleeTests::AddAbilitySystem(SourceCharacter, SourceAttributes);
	SourceAttributes->InitStrength(10.0f);
	UBaseAttributeSet* TargetAttributes = nullptr;
	UAbilitySystemComponent* TargetASC = StrengthMeleeTests::AddAbilitySystem(TargetCharacter, TargetAttributes);
	TargetAttributes->InitMaxHealth(100.0f);
	TargetAttributes->InitHealth(100.0f);

	Sword->SetOwner(SourceCharacter);
	Sword->SetInstigator(SourceCharacter);
	Sword->StatusEffectClasses = {
		UGASDamageInstantGameplayEffect::StaticClass(),
		UGASDamageInstantGameplayEffect::StaticClass()
	};

	FStrengthDamageRequest DamageRequest;
	DamageRequest.SourceASC = SourceASC;
	DamageRequest.DamageEffectClass = UGASDamageInstantGameplayEffect::StaticClass();
	DamageRequest.AttackCoefficient = 1.0f;
	DamageRequest.InstigatorActor = SourceCharacter;
	DamageRequest.EffectCauser = Sword;
	const FGameplayEffectSpecHandle DirectDamageSpec = UGASCombatLibrary::MakeStrengthDamageEffectSpec(DamageRequest);
	if (!TestTrue(TEXT("Sword starts a server hit window with a valid direct spec"), Sword->HitScanStart(DirectDamageSpec)))
	{
		return false;
	}

	TestEqual(TEXT("Sword builds every configured status spec"), Sword->CachedStatusEffectSpecHandles.Num(), 2);
	Sword->CachedStatusEffectSpecHandles[0].Data->SetSetByCallerMagnitude(Data_Damage, 2.0f);
	Sword->CachedStatusEffectSpecHandles[1].Data->SetSetByCallerMagnitude(Data_Damage, 3.0f);
	for (const FGameplayEffectSpecHandle& StatusSpec : Sword->CachedStatusEffectSpecHandles)
	{
		TestTrue(TEXT("Melee status spec retains the source ASC context"),
			StatusSpec.IsValid()
			&& StatusSpec.Data.IsValid()
			&& StatusSpec.Data->GetContext().GetOriginalInstigator() == SourceCharacter);
	}

	const float HealthBefore = TargetAttributes->GetHealth();
	const FHitResult HitResult(TargetCharacter, TargetCharacter->GetCapsuleComponent(), TargetCharacter->GetActorLocation(), FVector::UpVector);
	Sword->HandleHit(HitResult);
	TestEqual(TEXT("Melee direct damage is followed by both status payloads"), TargetAttributes->GetHealth(), HealthBefore - 15.0f);
	Sword->HandleHit(HitResult);
	TestEqual(TEXT("One melee attack does not apply twice to the same target"), TargetAttributes->GetHealth(), HealthBefore - 15.0f);
	Sword->HitScanEnd();
	return true;
}

#endif
