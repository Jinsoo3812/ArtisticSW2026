#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StoryTypes.h"
#include "StoryActionReceiverComponent.generated.h"

/**
 * Blueprint/C++ extension point for one-time story actions.
 *
 * Add a derived component to a server actor, list the action types it owns, and
 * implement ExecuteStoryAction. Returning true commits the action idempotency key.
 */
UCLASS(Abstract, Blueprintable, ClassGroup = (Story), meta = (BlueprintSpawnableComponent))
class STORY_API UStoryActionReceiverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStoryActionReceiverComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintPure, Category = "Story")
	bool CanHandleStoryAction(FGameplayTag ActionType) const;

	UFUNCTION(BlueprintNativeEvent, Category = "Story")
	bool ExecuteStoryAction(const FStoryActionSpec& Action);
	virtual bool ExecuteStoryAction_Implementation(const FStoryActionSpec& Action);

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Story")
	FGameplayTagContainer HandledActionTypes;
};
