#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "SWGameplayEffectContext.generated.h"

/**
 * Replicated, reference-frame-local path used by persistent path GameplayCues.
 * Keeping the endpoints local to a replicated actor makes gameplay and cosmetics
 * follow a moving platform without continuously replicating world positions.
 */
USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FSWPathCuePayload
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<AActor> ReferenceActor = nullptr;

	UPROPERTY(BlueprintReadOnly)
	FVector_NetQuantize100 StartLocal = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector_NetQuantize100 EndLocal = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector_NetQuantizeNormal SurfaceNormalLocal = FVector::UpVector;

	/** Gameplay corridor radius. Visuals may use it as their default half width. */
	UPROPERTY(BlueprintReadOnly)
	float CorridorRadius = 0.0f;

	/** Identifies one committed path when a cue class is reused. Zero is invalid. */
	UPROPERTY(BlueprintReadOnly)
	int32 InstanceId = 0;

	bool IsValid() const;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FSWPathCuePayload>
	: public TStructOpsTypeTraitsBase2<FSWPathCuePayload>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};

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

	void SetPathCuePayload(const FSWPathCuePayload& InPayload);
	void ClearPathCuePayload();
	bool HasPathCuePayload() const { return bHasPathCuePayload; }
	const FSWPathCuePayload& GetPathCuePayload() const { return PathCuePayload; }

private:
	UPROPERTY()
	FVector_NetQuantizeNormal ImpactDirection = FVector::ZeroVector;

	UPROPERTY()
	bool bHasImpactDirection = false;

	UPROPERTY()
	FSWPathCuePayload PathCuePayload;

	UPROPERTY()
	bool bHasPathCuePayload = false;
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
