#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "SWGameplayEffectContext.generated.h"

/** GameplayEffectContext carrying the authoritative incoming hit direction. */
USTRUCT()
struct ARTISTICSWCORE_API FSWGameplayEffectContext : public FGameplayEffectContext
{
	GENERATED_BODY()

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return StaticStruct();
	}

	virtual FSWGameplayEffectContext* Duplicate() const override;
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;

	void SetImpactDirection(const FVector& InDirection);
	void ClearImpactDirection();
	bool HasImpactDirection() const { return bHasImpactDirection; }
	FVector GetImpactDirection() const { return FVector(ImpactDirection); }

private:
	UPROPERTY()
	FVector_NetQuantizeNormal ImpactDirection = FVector::ZeroVector;

	UPROPERTY()
	bool bHasImpactDirection = false;
};

template<>
struct TStructOpsTypeTraits<FSWGameplayEffectContext>
	: public TStructOpsTypeTraitsBase2<FSWGameplayEffectContext>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};
