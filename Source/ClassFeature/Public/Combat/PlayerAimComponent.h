#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Components/ActorComponent.h"
#include "PlayerAimComponent.generated.h"

struct FGameplayEventData;

/** A view ray, not a client-authoritative hit. Travels with the release input event. */
USTRUCT()
struct CLASSFEATURE_API FGameplayAbilityTargetData_ViewRay : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantize10 Origin = FVector::ZeroVector;
	UPROPERTY()
	FVector_NetQuantizeNormal Direction = FVector::ForwardVector;

	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FGameplayAbilityTargetData_ViewRay> : TStructOpsTypeTraitsBase2<FGameplayAbilityTargetData_ViewRay>
{
	enum { WithNetSerializer = true, WithCopy = true };
};

/** Resolves screen-center aim; knows nothing about bow sockets, charging or damage. */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class CLASSFEATURE_API UPlayerAimComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UPlayerAimComponent();
	bool CaptureReleaseView(FGameplayEventData& EventData) const;
	bool ResolveReleaseAim(const FGameplayEventData& EventData, const AActor* Weapon,
		FVector& OutTarget, FVector& OutViewDirection) const;

protected:
	UPROPERTY(EditDefaultsOnly, Category="Aim", meta=(ClampMin="100"))
	float TraceDistance = 10000.f;
	/** Allows the third-person boom and network position error, without accepting arbitrary origins. */
	UPROPERTY(EditDefaultsOnly, Category="Aim|Validation", meta=(ClampMin="0"))
	float MaxViewOriginDistance = 1500.f;
	/** Camera lag and control-rotation replication can differ during a quick turn. */
	UPROPERTY(EditDefaultsOnly, Category="Aim|Validation", meta=(ClampMin="0", ClampMax="89"))
	float MaxViewAngleDegrees = 80.f;
};
