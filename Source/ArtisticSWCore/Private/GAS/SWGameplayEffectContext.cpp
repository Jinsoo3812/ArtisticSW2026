#include "GAS/SWGameplayEffectContext.h"

FSWGameplayEffectContext* FSWGameplayEffectContext::Duplicate() const
{
	FSWGameplayEffectContext* NewContext = new FSWGameplayEffectContext();
	*NewContext = *this;
	if (const FHitResult* ExistingHitResult = GetHitResult())
	{
		NewContext->AddHitResult(*ExistingHitResult, true);
	}
	return NewContext;
}

bool FSWGameplayEffectContext::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bool bParentSuccess = false;
	const bool bParentResult = FGameplayEffectContext::NetSerialize(Ar, Map, bParentSuccess);

	uint8 RepBits = 0;
	if (Ar.IsSaving() && bHasImpactDirection)
	{
		RepBits |= 1 << 0;
	}
	Ar.SerializeBits(&RepBits, 1);

	bool bDirectionSuccess = true;
	if (RepBits & (1 << 0))
	{
		ImpactDirection.NetSerialize(Ar, Map, bDirectionSuccess);
		bHasImpactDirection = true;
	}
	else if (Ar.IsLoading())
	{
		ClearImpactDirection();
	}

	bOutSuccess = bParentSuccess && bDirectionSuccess;
	return bParentResult && bDirectionSuccess;
}

void FSWGameplayEffectContext::SetImpactDirection(const FVector& InDirection)
{
	const FVector HorizontalDirection = InDirection.GetSafeNormal2D();
	bHasImpactDirection = !HorizontalDirection.IsNearlyZero();
	ImpactDirection = bHasImpactDirection ? HorizontalDirection : FVector::ZeroVector;
}

void FSWGameplayEffectContext::ClearImpactDirection()
{
	bHasImpactDirection = false;
	ImpactDirection = FVector::ZeroVector;
}
