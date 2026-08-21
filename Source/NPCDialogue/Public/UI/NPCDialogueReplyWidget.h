#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NPCDialogueTypes.h"
#include "NPCDialogueReplyWidget.generated.h"

class UButton;
class UNPCDialogueWidget;
class UTextBlock;

/** Native behavior for one Designer-authored reply row. */
UCLASS(Abstract, BlueprintType)
class NPCDIALOGUE_API UNPCDialogueReplyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	void InitializeReply(UNPCDialogueWidget* InOwnerWidget, const FNPCDialogueReply& InReply);

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Reply;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Reply;

private:
	UFUNCTION()
	void HandleClicked();

	TWeakObjectPtr<UNPCDialogueWidget> OwnerDialogueWidget;
	FName ReplyId = NAME_None;
};
