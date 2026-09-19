#include "GASAttributeDamageExecution.h"

#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "GASCombatLibrary.h"
#include "AbilitySystemComponent.h"

namespace AttributeDamageStatics
{
	struct FAttributeCaptures
	{
		DECLARE_ATTRIBUTE_CAPTUREDEF(Strength);

		FAttributeCaptures()
		{
			DEFINE_ATTRIBUTE_CAPTUREDEF(UBaseAttributeSet, Strength, Source, true);
		}
	};

	const FAttributeCaptures& Captures()
	{
		static FAttributeCaptures Instance;
		return Instance;
	}
}

UGASAttributeDamageExecution::UGASAttributeDamageExecution()
{
	RelevantAttributesToCapture.Add(AttributeDamageStatics::Captures().StrengthDef);
}

void UGASAttributeDamageExecution::Execute_Implementation(
	const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvaluationParameters.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	float Strength = 0.0f;
	if (!ExecutionParams.GetTargetAbilitySystemComponent() || !ExecutionParams.GetTargetAbilitySystemComponent()->IsOwnerActorAuthoritative()) return;
	const bool bCaptured = ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		AttributeDamageStatics::Captures().StrengthDef,
		EvaluationParameters,
		Strength);

	const float AttackCoefficient = Spec.GetSetByCallerMagnitude(
		Data_AttackCoefficient, false, 0.0f);
	const float ChargeMultiplier = Spec.GetSetByCallerMagnitude(
		Data_ChargeMultiplier, false, 0.0f);
	if (!bCaptured || !FMath::IsFinite(Strength) || !FMath::IsFinite(AttackCoefficient)
		|| !FMath::IsFinite(ChargeMultiplier) || AttackCoefficient <= 0.f || ChargeMultiplier <= 0.f) return;
	const float FinalDamage = UGASCombatLibrary::CalculateStrengthDamage(Strength, AttackCoefficient, ChargeMultiplier);
	if (!FMath::IsFinite(FinalDamage)) return;

	OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(
		UBaseAttributeSet::GetDamageAttribute(),
		EGameplayModOp::Additive,
		FinalDamage));
}
