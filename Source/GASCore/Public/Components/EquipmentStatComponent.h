#pragma once
#include "CoreMinimal.h"
#include "GAS/EquipmentStatModel.h"
#include "GameplayEffectTypes.h"
#include "EquipmentStatComponent.generated.h"

class UAbilitySystemComponent;
class UGameplayEffect;

/** Owns exactly one active weapon contribution. No inventory or presentation dependency. */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class GASCORE_API UEquipmentStatComponent : public UEquipmentStatModel
{
	GENERATED_BODY()
public:
	static UEquipmentStatComponent* GetOrCreate(AActor* Owner);
	bool Equip(UAbilitySystemComponent* ASC, AActor* Item, float Bonus, TSubclassOf<UGameplayEffect> EffectClass = nullptr);
	bool Clear();
	virtual bool IsEquipped(const AActor* Item) const override;
	bool RebindAbilitySystem(UAbilitySystemComponent* ASC);
	uint32 GetRevision() const { return Revision; }
	bool IsTransitioning() const { return bTransitioning; }
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UFUNCTION()
	void OnItemDestroyed(AActor* Item);
	UFUNCTION()
	void OnItemEndPlay(AActor* Item, EEndPlayReason::Type Reason);
	void OnDeathTagChanged(const FGameplayTag Tag, int32 Count);
	void Unbind();
	TWeakObjectPtr<UAbilitySystemComponent> AppliedASC;
	TWeakObjectPtr<AActor> EquippedItem;
	FActiveGameplayEffectHandle EffectHandle;
	FDelegateHandle DeathTagHandle;
	float AppliedBonus = 0.f;
	UPROPERTY(Transient)
	TSubclassOf<UGameplayEffect> AppliedEffectClass;
	uint32 Revision = 0;
	bool bTransitioning = false;
};
