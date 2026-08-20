#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Animation/AnimMontage.h"
#include "BasePlayer.h"
#include "GAS/Ability/GA_PlayerHitReaction.h"
#include "SWCharacterMovementComponent.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlayerHitReactionConfigurationTest,
	"ArtisticSW.GAS.HitReaction.PlayerConfiguration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerHitReactionConfigurationTest::RunTest(const FString& Parameters)
{
	const UClass* HitReactionClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/GameplayAbilitySystem/Ability/GA_HitReact.GA_HitReact_C"));
	if (!TestNotNull(TEXT("Player HitReaction Blueprint class loads"), HitReactionClass))
	{
		return false;
	}

	TestEqual(
		TEXT("Player HitReaction Blueprint directly inherits the player-specific native ability"),
		HitReactionClass->GetSuperClass(),
		UGA_PlayerHitReaction::StaticClass());

	const UGA_PlayerHitReaction* HitReactionCDO =
		HitReactionClass->GetDefaultObject<UGA_PlayerHitReaction>();
	for (const FName MontagePropertyName :
		{ FName(TEXT("FrontHitReactionMontage")), FName(TEXT("BackHitReactionMontage")) })
	{
		const FObjectProperty* MontageProperty = FindFProperty<FObjectProperty>(
			HitReactionClass, MontagePropertyName);
		const UAnimMontage* Montage = MontageProperty && HitReactionCDO
			? Cast<UAnimMontage>(MontageProperty->GetObjectPropertyValue_InContainer(HitReactionCDO))
			: nullptr;
		if (TestNotNull(*FString::Printf(TEXT("%s is configured"), *MontagePropertyName.ToString()), Montage))
		{
			const FTransform RootMotion = Montage->ExtractRootMotionFromTrackRange(
				0.0f, Montage->GetPlayLength(), FAnimExtractContext());
			AddInfo(FString::Printf(TEXT("%s root motion translation: %s"),
				*MontagePropertyName.ToString(), *RootMotion.GetTranslation().ToCompactString()));
		}
	}

	const FTransform AuthoredRootMotion(
		FQuat(FVector::UpVector, FMath::DegreesToRadians(5.0f)),
		FVector(30.0f, 40.0f, 7.0f));
	const FTransform RedirectedRootMotion =
		USWCharacterMovementComponent::RedirectRootMotionTranslation(
			AuthoredRootMotion, FVector(-2.0f, 0.0f, 1.0f));
	TestEqual(TEXT("Hit reaction keeps the authored horizontal root-motion distance"),
		static_cast<float>(RedirectedRootMotion.GetTranslation().Size2D()), 50.0f);
	TestEqual(TEXT("Hit reaction redirects root motion away from the damage source"),
		static_cast<float>(RedirectedRootMotion.GetTranslation().X), -50.0f);
	TestEqual(TEXT("Hit reaction preserves authored vertical root motion"),
		static_cast<float>(RedirectedRootMotion.GetTranslation().Z), 7.0f);
	TestTrue(TEXT("Hit reaction preserves authored root-motion rotation"),
		RedirectedRootMotion.GetRotation().Equals(AuthoredRootMotion.GetRotation()));

	USWCharacterMovementComponent* MovementComponent =
		NewObject<USWCharacterMovementComponent>();
	MovementComponent->BeginHitReactionRootMotion(FVector(3.0f, 4.0f, 12.0f));
	TestTrue(TEXT("Hit reaction root-motion redirection becomes active"),
		MovementComponent->IsRedirectingHitReactionRootMotion());
	TestTrue(TEXT("Hit reaction root-motion direction is horizontal and normalized"),
		MovementComponent->GetHitReactionRootMotionDirection().Equals(FVector(0.6f, 0.8f, 0.0f)));
	MovementComponent->EndHitReactionRootMotion();
	TestFalse(TEXT("Hit reaction root-motion redirection is cleared when the ability ends"),
		MovementComponent->IsRedirectingHitReactionRootMotion());

	const FFloatProperty* FallbackStrengthProperty = FindFProperty<FFloatProperty>(
		HitReactionClass, TEXT("FallbackRootMotionStrength"));
	const FFloatProperty* FallbackDurationProperty = FindFProperty<FFloatProperty>(
		HitReactionClass, TEXT("FallbackRootMotionDuration"));
	TestTrue(TEXT("Montages without authored root motion receive a fallback push"),
		FallbackStrengthProperty && FallbackDurationProperty && HitReactionCDO
		&& FallbackStrengthProperty->GetPropertyValue_InContainer(HitReactionCDO) > 0.0f
		&& FallbackDurationProperty->GetPropertyValue_InContainer(HitReactionCDO) > 0.0f);

	const UClass* PlayerClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/Blueprints/Player/BP_Player.BP_Player_C"));
	if (!TestNotNull(TEXT("Player Blueprint class loads"), PlayerClass))
	{
		return false;
	}

	const ABasePlayer* PlayerCDO = PlayerClass->GetDefaultObject<ABasePlayer>();
	if (!TestNotNull(TEXT("Player Blueprint CDO exists"), PlayerCDO))
	{
		return false;
	}

	const bool bGrantsHitReaction = PlayerCDO->DefaultGrantedAbilities.ContainsByPredicate(
		[HitReactionClass](const TSubclassOf<UGameplayAbility>& GrantedAbility)
		{
			return GrantedAbility.Get() == HitReactionClass;
		});
	TestTrue(
		TEXT("Player grants its HitReaction ability when possessed"),
		bGrantsHitReaction);

	return !HasAnyErrors();
}

#endif
