#include "GAS/SWGameplayEffectContext.h"

#include "Engine/PackageMapClient.h"

bool FSWPathCuePayload::IsValid() const
{
	return ::IsValid(ReferenceActor.Get())
		&& InstanceId != 0
		&& FVector::DistSquared(FVector(StartLocal), FVector(EndLocal)) > KINDA_SMALL_NUMBER
		&& !FVector(SurfaceNormalLocal).IsNearlyZero();
}

bool FSWPathCuePayload::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	UObject* ReferenceObject = ReferenceActor.Get();
	const bool bReferenceSuccess = Map
		&& Map->SerializeObject(Ar, AActor::StaticClass(), ReferenceObject);
	if (Ar.IsLoading())
	{
		ReferenceActor = Cast<AActor>(ReferenceObject);
	}

	bool bStartSuccess = true;
	bool bEndSuccess = true;
	bool bNormalSuccess = true;
	StartLocal.NetSerialize(Ar, Map, bStartSuccess);
	EndLocal.NetSerialize(Ar, Map, bEndSuccess);
	SurfaceNormalLocal.NetSerialize(Ar, Map, bNormalSuccess);
	Ar << CorridorRadius;
	Ar << InstanceId;

	bOutSuccess = bReferenceSuccess && bStartSuccess && bEndSuccess && bNormalSuccess;
	return bOutSuccess;
}

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
	if (Ar.IsSaving() && bHasPathCuePayload && PathCuePayload.IsValid())
	{
		RepBits |= 1 << 1;
	}
	Ar.SerializeBits(&RepBits, 2);

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

	bool bPathSuccess = true;
	if (RepBits & (1 << 1))
	{
		PathCuePayload.NetSerialize(Ar, Map, bPathSuccess);
		bHasPathCuePayload = PathCuePayload.IsValid();
	}
	else if (Ar.IsLoading())
	{
		ClearPathCuePayload();
	}

	bOutSuccess = bParentSuccess && bDirectionSuccess && bPathSuccess;
	return bParentResult && bDirectionSuccess && bPathSuccess;
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

void FSWGameplayEffectContext::SetPathCuePayload(const FSWPathCuePayload& InPayload)
{
	PathCuePayload = InPayload;
	bHasPathCuePayload = PathCuePayload.IsValid();
	if (!bHasPathCuePayload)
	{
		PathCuePayload = FSWPathCuePayload();
	}
}

void FSWGameplayEffectContext::ClearPathCuePayload()
{
	bHasPathCuePayload = false;
	PathCuePayload = FSWPathCuePayload();
}
