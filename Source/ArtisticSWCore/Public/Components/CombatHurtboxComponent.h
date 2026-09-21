#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CombatHurtboxComponent.generated.h"

UENUM(BlueprintType)
enum class ECombatHurtboxMode : uint8
{
	MovementCapsule,
	AnimatedPhysicsAsset
};

/** Owns direct-hit surfaces independently of GAS and movement collision. */
UCLASS(ClassGroup = Combat, meta = (BlueprintSpawnableComponent))
class ARTISTICSWCORE_API UCombatHurtboxComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UCombatHurtboxComponent();
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Hurtbox")
	ECombatHurtboxMode Mode = ECombatHurtboxMode::MovementCapsule;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Hurtbox")
	FName ProfileName = TEXT("CharacterHurtbox");
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Combat|Hurtbox")
	FString InitializationFailure;

	void InitializeHurtbox();
	bool IsAnimatedHurtboxReady() const;
	bool AcceptsHit(const FHitResult& Hit) const;
	static bool IsValidHitSurface(const AActor* Target, const FHitResult& Hit);
	bool ValidateConfiguration(FString& OutFailure) const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
private:
	bool bInitialized = false;
};
