#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BowComponent.generated.h"

class ABowItem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBowAimStateChangedSignature, bool, bIsAiming);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBowDrawAlphaChangedSignature, float, DrawAlpha);

UCLASS(ClassGroup=(Item), meta=(BlueprintSpawnableComponent))
class ARTISTICSWCORE_API UBowComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBowComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Bow")
	void SetAiming(bool bNewAiming);

	UFUNCTION(BlueprintCallable, Category = "Bow")
	void SetDrawAlpha(float NewDrawAlpha);

	UFUNCTION(BlueprintPure, Category = "Bow")
	bool IsAiming() const { return bIsAiming; }

	UFUNCTION(BlueprintPure, Category = "Bow")
	float GetDrawAlpha() const { return DrawAlpha; }

	UFUNCTION(BlueprintPure, Category = "Bow")
	float GetFireSpeed(float ReleaseDrawAlpha) const;

	/** Converges from the current socket position onto the release aim point, without gravity compensation. */
	bool TryBuildArrowLaunch(float FireSpeed, const FVector& AimTarget, const FVector& ViewDirection,
		FTransform& OutSpawnTransform, FVector& OutLaunchVelocity) const;

	/** Predicted locally and replicated from the server to all relevant clients. */
	UFUNCTION(BlueprintCallable, Category = "Bow")
	void SetArrowNocked(bool bNewArrowNocked);

	UFUNCTION(BlueprintPure, Category = "Bow")
	bool IsArrowNocked() const;

	UPROPERTY(BlueprintAssignable, Category = "Bow")
	FBowAimStateChangedSignature OnAimStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Bow")
	FBowDrawAlphaChangedSignature OnDrawAlphaChanged;

protected:
	UFUNCTION()
	void OnRep_IsAiming();

	UFUNCTION()
	void OnRep_DrawAlpha();

	UFUNCTION()
	void OnRep_ArrowNocked();

	ABowItem* GetOwningBow() const;
	void ApplyArrowNockedPresentation();
	bool IsLocallyControlledOwner() const;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Fire", meta = (ClampMin = "0.0"))
	float MinFireSpeed = 1500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Fire", meta = (ClampMin = "0.0"))
	float MaxFireSpeed = 4500.0f;

	UPROPERTY(ReplicatedUsing = OnRep_IsAiming, BlueprintReadOnly, Category = "Bow")
	bool bIsAiming = false;

	UPROPERTY(ReplicatedUsing = OnRep_DrawAlpha, BlueprintReadOnly, Category = "Bow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DrawAlpha = 0.0f;

	/** Server state for simulated proxies; autonomous clients apply the same state predictively. */
	UPROPERTY(ReplicatedUsing = OnRep_ArrowNocked, BlueprintReadOnly, Category = "Bow")
	bool bArrowNocked = false;

	/** Owning-client cosmetic prediction; authoritative state remains in bArrowNocked. */
	bool bPredictedArrowNocked = false;
};
