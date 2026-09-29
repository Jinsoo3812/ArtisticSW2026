#pragma once
#include "CoreMinimal.h"
#include "SWRoomAbilitySystemComponent.h"
#include "WeaponInputAbilitySystemComponent.generated.h"

/** Exact tag input routing shared by weapon, skill and interaction specs. */
UCLASS()
class GASCORE_API UWeaponInputAbilitySystemComponent : public USWRoomAbilitySystemComponent
{
	GENERATED_BODY()
public:
	void InputTagPressed(FGameplayTag InputTag);
	void InputTagReleased(FGameplayTag InputTag);
private:
	void RouteInput(FGameplayTag InputTag, bool bPressed);
};
