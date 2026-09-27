#pragma once
#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "WeaponInputAbilitySystemComponent.generated.h"

/** Exact tag input routing shared by weapon, skill and interaction specs. */
UCLASS()
class GASCORE_API UWeaponInputAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()
public:
	void InputTagPressed(FGameplayTag InputTag);
	void InputTagReleased(FGameplayTag InputTag);
private:
	void RouteInput(FGameplayTag InputTag, bool bPressed);
};
