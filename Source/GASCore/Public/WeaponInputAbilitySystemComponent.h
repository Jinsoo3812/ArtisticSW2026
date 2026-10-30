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
	static bool IsInputDiagnosticsEnabled();
	void LogInputDiagnostic(const TCHAR* Event, const FString& Detail) const;
	virtual void NotifyAbilityActivated(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability) override;
	virtual void NotifyAbilityFailed(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, const FGameplayTagContainer& FailureReason) override;
	virtual void NotifyAbilityEnded(FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, bool bWasCancelled) override;
	void InputTagPressed(FGameplayTag InputTag);
	void InputTagReleased(FGameplayTag InputTag);
private:
	void RouteInput(FGameplayTag InputTag, bool bPressed);
};
