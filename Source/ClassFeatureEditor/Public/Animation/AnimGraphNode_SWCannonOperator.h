#pragma once
#include "CoreMinimal.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "Animation/AnimNode_SWCannonOperator.h"
#include "AnimGraphNode_SWCannonOperator.generated.h"

UCLASS()
class CLASSFEATUREEDITOR_API UAnimGraphNode_SWCannonOperator : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category=Settings) FAnimNode_SWCannonOperator Node;
	virtual FText GetNodeTitle(ENodeTitleType::Type) const override { return FText::FromString(TEXT("Cannon Operator Pose")); }
protected:
	virtual FText GetControllerDescription() const override { return FText::FromString(TEXT("Cannon Operator")); }
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
