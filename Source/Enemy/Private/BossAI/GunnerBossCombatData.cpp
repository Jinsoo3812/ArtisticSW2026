#include "BossAI/GunnerBossCombatData.h"
#include "Animation/AnimMontage.h"
#include "CollisionChannels.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool FGunnerBossShotPattern::IsValid() const
{
	return FMath::IsFinite(PrepareTime) && PrepareTime > 0.f && FMath::IsFinite(LockTime)
		&& LockTime >= 0.f && LockTime <= PrepareTime && FMath::IsFinite(RecoveryTime) && RecoveryTime >= 0.f
		&& FMath::IsFinite(ShotGap) && ShotGap >= 0.f && FMath::IsFinite(Cooldown) && Cooldown >= 0.f
		&& FMath::IsFinite(PostAttackGap) && PostAttackGap >= 0.f && FMath::IsFinite(DamageCoefficient)
		&& DamageCoefficient > 0.f && FMath::IsFinite(MontagePlayRate) && MontagePlayRate > 0.f
		&& (!Montage || ((PrepareSection.IsNone() || Montage->GetSectionIndex(PrepareSection) != INDEX_NONE)
			&& (FireSection.IsNone() || Montage->GetSectionIndex(FireSection) != INDEX_NONE)
			&& (RecoverySection.IsNone() || Montage->GetSectionIndex(RecoverySection) != INDEX_NONE)));
}

bool FGunnerBossRaySettings::IsValid() const
{
	return FMath::IsFinite(Color.R) && FMath::IsFinite(Color.G) && FMath::IsFinite(Color.B) && FMath::IsFinite(Color.A)
		&& FMath::IsFinite(Thickness) && Thickness > 0.f && FMath::IsFinite(ShotThickness) && ShotThickness > 0.f
		&& FMath::IsFinite(ShotLifetime) && ShotLifetime > 0.f
		&& FMath::IsFinite(EndPresentationLifetime) && EndPresentationLifetime > 0.f
		&& FMath::IsFinite(EndpointInterpolationSpeed) && EndpointInterpolationSpeed >= 0.f;
}

UGunnerBossCombatData::UGunnerBossCombatData()
{
	TraceChannel = ECC_WeaponAim;
	Burst.PrepareTime = 1.f;
	Burst.LockTime = 0.35f;
	Burst.RecoveryTime = 2.5f;
	Burst.Cooldown = 12.f;
	Burst.DamageCoefficient = 0.7f;
	Burst.PostAttackGap = 0.f;
}

bool UGunnerBossCombatData::Validate(FString& OutError) const
{
	if (FireSocketName.IsNone() || !FMath::IsFinite(AttackRange) || AttackRange <= 0.f
		|| TargetAimOffset.ContainsNaN() || !FMath::IsFinite(AimUpdateInterval) || AimUpdateInterval <= 0.f
		|| !Basic.IsValid() || !Burst.IsValid() || !Ray.IsValid() || BurstShotCount < 1 || BurstShotCount > 16)
	{
		OutError = TEXT("Invalid gun/pattern/ray settings: require 0 <= LockTime <= PrepareTime, valid montage sections and finite positive range/line thickness.");
		return false;
	}
	return true;
}

#if WITH_EDITOR
EDataValidationResult UGunnerBossCombatData::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Parent = Super::IsDataValid(Context);
	FString Error;
	if (!Validate(Error))
	{
		Context.AddError(FText::FromString(Error));
		return EDataValidationResult::Invalid;
	}
	return Parent == EDataValidationResult::Invalid ? Parent : EDataValidationResult::Valid;
}
#endif
