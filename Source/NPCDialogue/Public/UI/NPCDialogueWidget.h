#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NPCDialogueTypes.h"
#include "NPCDialogueWidget.generated.h"

class UNPCDialogueReplyWidget;
class UPlayerDialogueComponent;
class UTextBlock;
class UVerticalBox;

/**
 * Native dialogue presentation. Child WBP assets only provide named Designer widgets and style.
 * No Blueprint graph binding or property binding is required.
 */
UCLASS(Abstract, BlueprintType)
class NPCDIALOGUE_API UNPCDialogueWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UNPCDialogueWidget(const FObjectInitializer& ObjectInitializer);

	virtual FReply NativeOnMouseButtonDown(
		const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnKeyDown(
		const FGeometry& InGeometry,
		const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnPreviewKeyDown(
		const FGeometry& InGeometry,
		const FKeyEvent& InKeyEvent) override;

	void InitializeDialogue(UPlayerDialogueComponent* InDialogueComponent, const FNPCDialogueView& InView);
	void ApplyDialogueView(const FNPCDialogueView& InView);
	void SelectReply(FName ReplyId);
	void SetReplyWidgetClass(TSubclassOf<UNPCDialogueReplyWidget> InClass) { ReplyWidgetClass = InClass; }
	TSubclassOf<UNPCDialogueReplyWidget> GetReplyWidgetClass() const { return ReplyWidgetClass; }

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_NPCName;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Dialogue;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UVerticalBox> VerticalBox_Replies;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_ContinueHint;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Dialogue|Designer")
	TSubclassOf<UNPCDialogueReplyWidget> ReplyWidgetClass;

private:
	void RebuildReplies();
	bool CanAdvanceDirectly() const { return CurrentView.Replies.IsEmpty(); }

	TWeakObjectPtr<UPlayerDialogueComponent> DialogueComponent;
	FNPCDialogueView CurrentView;
};
