#include "WaterBombCannonball.h"

#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseCharacter.h"
#include "BaseGameplayTags.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameplayEffect.h"
#include "Ship.h"
#include "WaterBombEffects.h"
#include "GAS/SWCombatEffectContextLibrary.h"

AWaterBombCannonball::AWaterBombCannonball()
{
	AttackSpeedEffectClass = UWaterBombAttackSpeedGameplayEffect::StaticClass();
	CannonDisableEffectClass = UWaterBombCannonDisableGameplayEffect::StaticClass();
}

void AWaterBombCannonball::ConfigureFromAbility(float InEffectDurationSeconds, float InAttackSpeedMultiplier)
{
	if (!HasAuthority())
	{
		return;
	}

	EffectDurationSeconds = FMath::Max(0.1f, InEffectDurationSeconds);
	AttackSpeedMultiplier = FMath::Clamp(InAttackSpeedMultiplier, 0.1f, 1.0f);
}

void AWaterBombCannonball::HandleShipHit(AShip* HitShip)
{
	AShip* SourceShip = GetLaunchingShip();
	if (!HasAuthority() || !HitShip || HitShip == SourceShip)
	{
		return;
	}

	// 현재 스킬은 플레이어가 적함에 맞힌 경우만 유효합니다.
	if (!HitShip->IsEnemyShipForEffects()
		|| (SourceShip && SourceShip->IsEnemyShipForEffects()))
	{
		return;
	}

	int32 SlowedEnemyCount = 0;
	bool bCannonDisabled = false;
	if (UAbilitySystemComponent* ShipASC = HitShip->GetAbilitySystemComponent())
	{
		bCannonDisabled = ApplyTimedEffect(
			ShipASC,
			CannonDisableEffectClass,
			State_Ship_CannonDisabled);
	}

	for (TActorIterator<ABaseCharacter> It(GetWorld()); It; ++It)
	{
		ABaseCharacter* Character = *It;
		if (!Character || !Character->IsEnemyCharacterForEffects() || !IsCharacterOnShip(Character, HitShip))
		{
			continue;
		}

		UAbilitySystemComponent* CharacterASC = Character->GetAbilitySystemComponent();
		if (!CharacterASC
			|| CharacterASC->HasMatchingGameplayTag(State_Dead)
			|| CharacterASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()) <= 0.0f)
		{
			continue;
		}

		if (ApplyTimedEffect(
			CharacterASC,
			AttackSpeedEffectClass,
			State_Debuff_WaterBomb,
			FMath::Clamp(AttackSpeedMultiplier, 0.1f, 1.0f)))
		{
			++SlowedEnemyCount;
			UE_LOG(LogTemp, Log,
				TEXT("[WaterBomb] Slowed onboard enemy=%s, movement-base=%s, multiplier=%.2f"),
				*Character->GetName(),
				*GetNameSafe(APawn::GetMovementBaseActor(Character)),
				FMath::Clamp(AttackSpeedMultiplier, 0.1f, 1.0f));
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[WaterBomb] Hit enemy ship=%s, duration=%.2fs, attack-speed multiplier=%.2f, cannons disabled=%s, onboard enemies slowed=%d"),
		*HitShip->GetName(),
		FMath::Max(0.1f, EffectDurationSeconds),
		FMath::Clamp(AttackSpeedMultiplier, 0.1f, 1.0f),
		bCannonDisabled ? TEXT("YES") : TEXT("NO"),
		SlowedEnemyCount);

	Destroy();
}

bool AWaterBombCannonball::IsCharacterOnShip(const ABaseCharacter* Character, const AShip* Ship) const
{
	if (!Character || !Ship)
	{
		return false;
	}

	if (Character->IsBasedOnActor(Ship) || APawn::GetMovementBaseActor(Character) == Ship)
	{
		return true;
	}

	for (const AActor* Parent = Character->GetAttachParentActor(); Parent; Parent = Parent->GetAttachParentActor())
	{
		if (Parent == Ship)
		{
			return true;
		}
	}

	return false;
}

bool AWaterBombCannonball::ApplyTimedEffect(
	UAbilitySystemComponent* TargetASC,
	TSubclassOf<UGameplayEffect> EffectClass,
	const FGameplayTag& GrantedTag,
	TOptional<float> SetByCallerAttackSpeedMultiplier) const
{
	if (!TargetASC || !EffectClass || !GrantedTag.IsValid())
	{
		return false;
	}

	FGameplayTagContainer ExistingEffectTags;
	ExistingEffectTags.AddTag(GrantedTag);
	TargetASC->RemoveActiveEffectsWithGrantedTags(ExistingEffectTags);

	UAbilitySystemComponent* SourceASC = GetLaunchingShip()
		? GetLaunchingShip()->GetAbilitySystemComponent()
		: nullptr;
	if (!SourceASC)
	{
		SourceASC = TargetASC;
	}

	FGameplayEffectContextHandle Context =
		USWCombatEffectContextLibrary::MakeCombatEffectContext(
			SourceASC,
			GetInstigator(),
			const_cast<AWaterBombCannonball*>(this),
			TargetASC->GetAvatarActor(),
			false,
			FHitResult(),
			GetVelocity());

	FGameplayEffectSpecHandle SpecHandle = SourceASC->MakeOutgoingSpec(EffectClass, 1.0f, Context);
	if (!SpecHandle.IsValid())
	{
		return false;
	}

	FGameplayEffectSpec& Spec = *SpecHandle.Data.Get();
	Spec.SetDuration(FMath::Max(0.1f, EffectDurationSeconds), true);
	Spec.DynamicGrantedTags.AddTag(GrantedTag);
	if (SetByCallerAttackSpeedMultiplier.IsSet())
	{
		Spec.SetSetByCallerMagnitude(
			Data_Effect_AttackSpeedMultiplier,
			FMath::Clamp(SetByCallerAttackSpeedMultiplier.GetValue(), 0.1f, 1.0f));
	}

	return TargetASC->ApplyGameplayEffectSpecToSelf(Spec).IsValid();
}
