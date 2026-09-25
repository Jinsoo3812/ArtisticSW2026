#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CombatPresentationComponent.generated.h"

class UAbilitySystemComponent;
struct FOnAttributeChangeData;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCombatStrengthChanged, float, Strength);

/** Local presenter: reads replicated attributes and drives animation/UI, never writes stats. */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class GASCORE_API UCombatPresentationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	static UCombatPresentationComponent* GetOrCreate(AActor* Owner);
	void Initialize(UAbilitySystemComponent* ASC);
	void Uninitialize();
	UFUNCTION(BlueprintPure, Category="Combat")
	float GetStrength() const;
	UPROPERTY(BlueprintAssignable, Category="Combat")
	FOnCombatStrengthChanged OnStrengthChanged;
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	void StrengthChanged(const FOnAttributeChangeData& Data);
	void AttackSpeedChanged(const FOnAttributeChangeData& Data);
	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystem;
	FDelegateHandle StrengthHandle;
	FDelegateHandle AttackSpeedHandle;
};
