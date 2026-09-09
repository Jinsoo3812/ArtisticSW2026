#include "Skills/AreaSlowSkillDataAsset.h"

#include "BaseGameplayTags.h"
#include "Effects/AreaSlowGameplayEffect.h"
#include "Skills/AreaSlowDecalActors.h"

UAreaSlowSkillDataAsset::UAreaSlowSkillDataAsset()
{
	FGameplayTagQueryExpression RequiredExpression;
	RequiredExpression.AllTagsMatch()
		.AddTag(Targetable_Skill_AreaSlow)
		.AddTag(Capability_Effect_MoveSpeedMultiplier)
		.AddTag(Capability_Effect_AttackSpeedMultiplier);
	RequiredTargetQuery.Build(RequiredExpression);

	FGameplayTagQueryExpression BlockedExpression;
	BlockedExpression.AnyTagsMatch()
		.AddTag(Immunity_Debuff_Slow)
		.AddTag(State_Dead);
	BlockedTargetQuery.Build(BlockedExpression);

	TargetObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));
	SlowEffectClass = UAreaSlowGameplayEffect::StaticClass();
	TargetingDecalClass = AAreaSlowTargetingDecal::StaticClass();
	ConfirmedDecalClass = AAreaSlowConfirmedDecal::StaticClass();
}

FAreaSlowRange UAreaSlowSkillDataAsset::BuildRangeForTransform(
	const FTransform& SourceTransform,
	float InFrontGap,
	float InRangeLength,
	float InRangeWidth,
	float InRangeHeight,
	float InVerticalOffset)
{
	const float SafeLength = FMath::Max(0.0f, InRangeLength);
	const float SafeWidth = FMath::Max(0.0f, InRangeWidth);
	const float SafeHeight = FMath::Max(0.0f, InRangeHeight);
	const FRotator YawRotation(0.0f, SourceTransform.Rotator().Yaw, 0.0f);
	const FVector Forward = YawRotation.Vector();

	FAreaSlowRange Result;
	Result.Rotation = YawRotation;
	Result.BoxExtent = FVector(SafeLength * 0.5f, SafeWidth * 0.5f, SafeHeight * 0.5f);
	Result.Center = SourceTransform.GetLocation()
		+ Forward * (FMath::Max(0.0f, InFrontGap) + SafeLength * 0.5f)
		+ FVector::UpVector * InVerticalOffset;
	return Result;
}

FAreaSlowRange UAreaSlowSkillDataAsset::BuildRangeForActor(const AActor* SourceActor) const
{
	return SourceActor
		? BuildRangeForTransform(
			SourceActor->GetActorTransform(),
			FrontGap,
			RangeLength,
			RangeWidth,
			RangeHeight,
			VerticalOffset)
		: FAreaSlowRange();
}

bool UAreaSlowSkillDataAsset::IsRuntimeConfigValid(FString* OutFailureReason) const
{
	auto Fail = [OutFailureReason](const TCHAR* Reason)
	{
		if (OutFailureReason)
		{
			*OutFailureReason = Reason;
		}
		return false;
	};

	if (RangeLength <= 0.0f || RangeWidth <= 0.0f || RangeHeight <= 0.0f)
	{
		return Fail(TEXT("Range length, width, and height must all be greater than zero."));
	}
	if (MoveSpeedMultiplier < 0.1f || MoveSpeedMultiplier > 1.0f)
	{
		return Fail(TEXT("MoveSpeedMultiplier must be in the inclusive range [0.1, 1.0]."));
	}
	if (AttackSpeedMultiplier < 0.1f || AttackSpeedMultiplier > 1.0f)
	{
		return Fail(TEXT("AttackSpeedMultiplier must be in the inclusive range [0.1, 1.0]."));
	}
	if (SlowDuration <= 0.0f)
	{
		return Fail(TEXT("SlowDuration must be greater than zero."));
	}
	if (!SlowEffectClass)
	{
		return Fail(TEXT("SlowEffectClass is not assigned."));
	}
	if (RequiredTargetQuery.IsEmpty())
	{
		return Fail(TEXT("RequiredTargetQuery is empty; the skill must be explicit opt-in."));
	}
	if (TargetObjectTypes.IsEmpty())
	{
		return Fail(TEXT("TargetObjectTypes is empty."));
	}
	return true;
}
