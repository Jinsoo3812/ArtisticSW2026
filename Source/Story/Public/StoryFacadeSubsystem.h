#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "StoryFacadeSubsystem.generated.h"

class UStorySubsystem;

/**
 * Persistent events from the story flow chart.
 *
 * The vertical position of an enum entry has no meaning. Dependencies are
 * validated by UStoryFacadeSubsystem when CompleteStoryNode is called.
 */
UENUM(BlueprintType)
enum class EStoryNode : uint8
{
	GameStarted UMETA(DisplayName = "게임 시작"),
	FirstSailingCompleted UMETA(DisplayName = "첫 출항 완료"),
	ReconQuestAccepted UMETA(DisplayName = "정찰 퀘스트 수락"),
	StoryClue1Acquired UMETA(DisplayName = "스토리 단서 1 획득"),
	CipherBookAcquired UMETA(DisplayName = "일본군 암호 해독서 획득"),
	MiddleBoss1Defeated UMETA(DisplayName = "중간보스 1 처치 및 명량 참공 지도 획득"),
	SupplyPatrolQuestAccepted UMETA(DisplayName = "보급로 순찰 퀘스트 수락"),
	CurrentGeneratorUnlocked UMETA(DisplayName = "해류 발생기 해금"),
	MiddleBoss2Defeated UMETA(DisplayName = "중간보스 2 처치 및 일본군 암호 획득"),
	SuppressJapaneseForcesQuestAccepted
		UMETA(DisplayName = "해독된 암호 제작 및 일본군 저지 퀘스트 수락"),
	StormUnlocked UMETA(DisplayName = "폭풍 해금"),
	MiddleBoss3Defeated UMETA(DisplayName = "중간보스 3 처치 및 일본군 증원 정보 획득"),
	UldolmokBattleQuestAccepted UMETA(DisplayName = "울돌목 전투 퀘스트 수락"),
	FlamethrowerUnlocked UMETA(DisplayName = "화염방사기 해금"),
	FinalBossDefeated UMETA(DisplayName = "최종 보스 처치"),
	EndingDialogueCompleted UMETA(DisplayName = "엔딩 대화 완료")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStoryFacadeChanged);

/**
 * The only Story API ordinary gameplay modules should use.
 *
 * External modules report a durable event with CompleteStoryNode and gate
 * their content with IsStoryNodeReached. Gameplay Tags remain private.
 */
UCLASS()
class STORY_API UStoryFacadeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Clears previous progress and completes GameStarted. Server only. */
	UFUNCTION(BlueprintCallable, Category = "Story")
	bool StartNewCampaign();

	/**
	 * Completes one node if all arrow dependencies have been reached.
	 * This is idempotent: an already reached node also returns true.
	 * Story mutations must be called on the server.
	 */
	UFUNCTION(BlueprintCallable, Category = "Story")
	bool CompleteStoryNode(EStoryNode Node);

	/** True from completion onward, including after save/load and replication. */
	UFUNCTION(BlueprintPure, Category = "Story")
	bool IsStoryNodeReached(EStoryNode Node) const;

	/** True when Node is already complete or all of its arrow prerequisites are complete. */
	UFUNCTION(BlueprintPure, Category = "Story")
	bool CanCompleteStoryNode(EStoryNode Node) const;

	UFUNCTION(BlueprintCallable, Category = "Story|Persistence")
	bool SaveCampaign(const FString& SlotName = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "Story|Persistence")
	bool LoadCampaign(const FString& SlotName = TEXT(""));

	/** C++ automation/use-case seam; normal game code never needs this. */
	void ConfigureForUseCase(UStorySubsystem* StorySubsystem);

	UPROPERTY(BlueprintAssignable, Category = "Story")
	FOnStoryFacadeChanged OnStoryChanged;

private:
	UFUNCTION()
	void HandleStoryProgressChanged(int32 Revision);

	UStorySubsystem* ResolveStory() const;
	bool ArePrerequisitesReached(EStoryNode Node) const;
	FGameplayTag GetInternalTag(EStoryNode Node) const;

	TWeakObjectPtr<UStorySubsystem> StoryOverride;
};
