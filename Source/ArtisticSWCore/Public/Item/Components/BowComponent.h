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
	float GetCurrentFireSpeed() const;

	UFUNCTION(BlueprintPure, Category = "Bow")
	FTransform BuildArrowSpawnTransform() const;

	UPROPERTY(BlueprintAssignable, Category = "Bow")
	FBowAimStateChangedSignature OnAimStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Bow")
	FBowDrawAlphaChangedSignature OnDrawAlphaChanged;

protected:
	UFUNCTION(Server, Reliable)
	void ServerSetAiming(bool bNewAiming);

	UFUNCTION(Server, Reliable)
	void ServerSetDrawAlpha(float NewDrawAlpha);

	UFUNCTION()
	void OnRep_IsAiming();

	UFUNCTION()
	void OnRep_DrawAlpha();

	ABowItem* GetOwningBow() const;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Fire", meta = (ClampMin = "0.0"))
	float MinFireSpeed = 1500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Fire", meta = (ClampMin = "0.0"))
	float MaxFireSpeed = 4500.0f;

	UPROPERTY(ReplicatedUsing = OnRep_IsAiming, BlueprintReadOnly, Category = "Bow")
	bool bIsAiming = false;

	UPROPERTY(ReplicatedUsing = OnRep_DrawAlpha, BlueprintReadOnly, Category = "Bow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DrawAlpha = 0.0f;
};
