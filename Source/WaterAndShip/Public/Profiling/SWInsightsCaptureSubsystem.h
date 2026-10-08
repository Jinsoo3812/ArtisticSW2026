#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "SWInsightsCaptureSubsystem.generated.h"

class FJsonObject;

/** Opt-in, bounded Insights captures. Inactive worlds have no profiler tick. */
UCLASS()
class WATERANDSHIP_API USWInsightsCaptureSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	bool StartCapture(const FString& Label, float Seconds);
	void MarkCapture(const FString& Label);
	void StopCapture();

private:
	FTSTicker::FDelegateHandle ScheduledCapture;
	FString TracePath;
	FString AddedChannels;
	TSharedPtr<FJsonObject> Metadata;
	double CaptureStartSeconds = 0.0;
	bool bAddedNamedEvents = false;
	bool bAutoQuit = false;
	bool OwnsConnection() const;
	void WriteMetadata() const;
};
