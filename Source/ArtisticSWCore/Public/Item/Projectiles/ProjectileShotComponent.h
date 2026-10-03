#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ProjectileShotComponent.generated.h"

enum class EProjectileShotCommit : uint8 { Pending, Succeeded, Rejected };
DECLARE_DELEGATE_RetVal(EProjectileShotCommit, FProjectileShotCommitDelegate);
DECLARE_DELEGATE_OneParam(FProjectileShotFinishedDelegate, bool);

/** One active weapon release per shooter. Notify requests commit; late tick performs it. */
UCLASS()
class ARTISTICSWCORE_API UProjectileShotComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UProjectileShotComponent();
	static UProjectileShotComponent* FindOrAdd(AActor* Shooter);
	bool Queue(UObject* RequestOwner, const FGuid& ShotId, FProjectileShotCommitDelegate Commit,
		FProjectileShotFinishedDelegate Finished, float Timeout = 1.0f);
	void Cancel(const FGuid& ShotId);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	void ResetRequest();
	FGuid PendingId;
	TWeakObjectPtr<UObject> PendingOwner;
	FProjectileShotCommitDelegate CommitDelegate;
	FProjectileShotFinishedDelegate FinishedDelegate;
	double Deadline = 0.0;
};
