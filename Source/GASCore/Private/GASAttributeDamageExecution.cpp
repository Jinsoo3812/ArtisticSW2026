#include "GASAttributeDamageExecution.h"

#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"

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
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		AttributeDamageStatics::Captures().StrengthDef,
		EvaluationParameters,
		Strength);

	const float AttackCoefficient = Spec.GetSetByCallerMagnitude(
		Data_AttackCoefficient, false, 1.0f);
	const float ChargeMultiplier = Spec.GetSetByCallerMagnitude(
		Data_ChargeMultiplier, false, 1.0f);
	const float FinalDamage = FMath::Max(
		1.0f,
		FMath::Max(0.0f, Strength)
		* FMath::Max(0.0f, AttackCoefficient)
		* FMath::Max(0.0f, ChargeMultiplier));

	OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(
		UBaseAttributeSet::GetDamageAttribute(),
		EGameplayModOp::Additive,
		FinalDamage));
}
