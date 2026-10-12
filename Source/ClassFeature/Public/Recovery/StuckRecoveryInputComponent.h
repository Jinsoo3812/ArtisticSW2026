#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "StuckRecoveryInputComponent.generated.h"

class UEnhancedInputComponent;
class UInputAction;
class UStuckRecoveryComponent;
class UStuckRecoveryWidget;

/** Local input and presentation adapter; owns no collision or relocation decisions. */
UCLASS(ClassGroup=(Input), meta=(BlueprintSpawnableComponent))
class CLASSFEATURE_API UStuckRecoveryInputComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UStuckRecoveryInputComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	void BindInput(UEnhancedInputComponent* Input);

	/** Boolean action in the on-foot IMC, with no Hold/Pressed/Pulse trigger. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stuck Recovery|Input")
	TObjectPtr<UInputAction> EscapeAction;
	/** Direct editor key option when EscapeAction is unassigned. No key is reserved by default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stuck Recovery|Input")
	FKey EscapeKey;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stuck Recovery|UI")
	bool bShowGuide = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stuck Recovery|UI")
	TSubclassOf<UStuckRecoveryWidget> GuideWidgetClass;
private:
	void Pressed();
	void Released();
	void RemoveBindings();
	void RefreshGuide(bool bVisible);
	FText GetKeyLabel() const;
	UFUNCTION()
	void HandleRecoveryResult(bool bSucceeded);
	TWeakObjectPtr<UStuckRecoveryComponent> Recovery;
	TWeakObjectPtr<UEnhancedInputComponent> BoundInput;
	TArray<uint32> BindingHandles;
	UPROPERTY(Transient)
	TObjectPtr<UStuckRecoveryWidget> Guide;
	bool bRawKeyWasDown = false;
	bool bInputAccepted = false;
};
