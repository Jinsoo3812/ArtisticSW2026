#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWDevTestInputWidget.generated.h"
UCLASS()
class CLASSFEATURE_API USWDevTestInputWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 void SetGuide(const FString& Text);
 void SetResult(const FString& Text);
protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
 virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
private:
 TSharedPtr<class STextBlock> Label;
 FString Guide, Result;
 double ResultUntil = 0;
};
