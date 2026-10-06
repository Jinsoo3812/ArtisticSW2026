#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Room/SWVoyageResetParticipant.h"
#include "SWNetworkWeatherActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UTexture;
class UCurveFloat;
class UMaterialInterface;
struct FStreamableHandle;
class UPackageMap;

UENUM()
enum class ESWWeatherAssetKind : uint8 { SkyMaterial, ColorCurve, ScalarCurve };
UENUM()
enum class ESWWeatherSegmentReadiness : uint8 { PendingAssets, Ready, Invalid };
UENUM()
enum class ESWWeatherPlaybackStatus : uint8
{
	WaitingInitialState, Playing, HoldingForData, Frozen, WaitingEpochReset, WaitingRecoveryTarget, Recovering
};

USTRUCT()
struct FSWWeatherAssetReference
{
	GENERATED_BODY()
	UPROPERTY() FString MemberPath;
	UPROPERTY() FString ObjectPath;
	UPROPERTY() ESWWeatherAssetKind Kind = ESWWeatherAssetKind::SkyMaterial;
	UPROPERTY() bool bIsNull = false;
};

USTRUCT()
struct FSWWeatherPropertyValue
{
	GENERATED_BODY()
	UPROPERTY() FName Name;
	UPROPERTY() FString Value;
	UPROPERTY() TArray<FSWWeatherAssetReference> References;
};

USTRUCT()
struct FSWWeatherScalarValue
{
	GENERATED_BODY()
	UPROPERTY() FName Name;
	UPROPERTY() float Value = 0.f;
};

USTRUCT()
struct FSWWeatherVectorValue
{
	GENERATED_BODY()
	UPROPERTY() FName Name;
	UPROPERTY() FLinearColor Value = FLinearColor::Black;
};

USTRUCT()
struct FSWWeatherTextureValue
{
	GENERATED_BODY()
	UPROPERTY() FName Name;
	UPROPERTY() FString ObjectPath;
	UPROPERTY() bool bIsNull = false;
};

USTRUCT()
struct FSWWeatherMaterialState
{
	GENERATED_BODY()
	UPROPERTY() FName PropertyName;
	UPROPERTY() TArray<FSWWeatherScalarValue> Scalars;
	UPROPERTY() TArray<FSWWeatherVectorValue> Vectors;
	UPROPERTY() TArray<FSWWeatherTextureValue> Textures;
};

USTRUCT()
struct FSWWeatherPlaybackSegment
{
	GENERATED_BODY()
	UPROPERTY() uint32 Epoch = 0;
	UPROPERTY() uint32 Sequence = 0;
	UPROPERTY() int32 Hour = 0;
	UPROPERTY() double StartServerTime = 0.;
	UPROPERTY() double SecondsPerHour = 10.;
	UPROPERTY() double FrozenAlpha = 0.;
	UPROPERTY() float SunVisibilityFrom = 1.f;
	UPROPERTY() float SunVisibilityTo = 1.f;
	UPROPERTY() TArray<FSWWeatherPropertyValue> Properties;
	UPROPERTY() TArray<FSWWeatherMaterialState> Materials;
};

USTRUCT()
struct FSWWeatherPlaybackWindow
{
	GENERATED_BODY()
	UPROPERTY() int32 Generation = 0;
	UPROPERTY() uint32 Epoch = 0;
	UPROPERTY() double PublishedServerTime = 0.;
	UPROPERTY() double EpochPlaybackStartServerTime = 0.;
	UPROPERTY() double PlaybackDelaySeconds = 2.;
	UPROPERTY() TArray<FSWWeatherPlaybackSegment> Segments;
	// Prepared once at publication, shared by connection serializers.
	TArray<uint8> CompressedPayload;
	uint32 RawPayloadBytes = 0;
	bool PreparePayload();
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<> struct TStructOpsTypeTraits<FSWWeatherPlaybackWindow> : TStructOpsTypeTraitsBase2<FSWWeatherPlaybackWindow>
{
	enum { WithNetSerializer = true };
};

USTRUCT()
struct FSWWeatherVisualFrame
{
	GENERATED_BODY()
	UPROPERTY() TArray<FSWWeatherMaterialState> Materials;
	UPROPERTY() FLinearColor DirectionalColor = FLinearColor::Black;
	UPROPERTY() FLinearColor SkyLightColor = FLinearColor::Black;
	UPROPERTY() FLinearColor FogColor = FLinearColor::Black;
	UPROPERTY() FLinearColor DirectionalFogColor = FLinearColor::Black;
	UPROPERTY() FQuat DirectionalRotation = FQuat::Identity;
	UPROPERTY() FQuat UnclampedSunRotation = FQuat::Identity;
	UPROPERTY() FVector BillboardLocation = FVector::ZeroVector;
	UPROPERTY() FVector BillboardScale = FVector::OneVector;
	UPROPERTY() TObjectPtr<UMaterialInterface> BillboardMaterial;
	UPROPERTY() float SunVisible = 1.f;
	UPROPERTY() float Darkness = 0.f;
};

USTRUCT()
struct FSWWeatherWindState
{
	GENERATED_BODY()
	UPROPERTY() int32 Generation = 0;
	UPROPERTY() bool bInitialized = false;
	UPROPERTY() double StartServerTime = 0.;
	UPROPERTY() double Duration = 1.;
	UPROPERTY() double Speed = 0.005;
	UPROPERTY() FVector From = FVector(1., 0., 0.);
	UPROPERTY() FVector To = FVector(1., 0., 0.);
	UPROPERTY() FVector OffsetAtStart = FVector::ZeroVector;
};

/** Native clock/authority adapter for the copied StylizedWeather Blueprint. */
UCLASS(Blueprintable)
class WATERANDSHIP_API ASWNetworkWeatherActor : public AActor, public ISWVoyageResetParticipant
{
	GENERATED_BODY()
public:
	ASWNetworkWeatherActor();
	virtual ESWVoyagePolicy GetVoyagePolicy_Implementation() const override { return ESWVoyagePolicy::ResetParticipant; }
	virtual FName GetVoyageParticipantId_Implementation() const override;
	virtual ESWVoyageStepResult PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	UPROPERTY(EditDefaultsOnly, Category = "Weather|Network")
	TObjectPtr<UCurveFloat> SunVisibilityCurve;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Weather|Network")
	double ServerWeatherRandomFloat(double Min, double Max) const;
	UFUNCTION(BlueprintPure, Category = "Weather|Network")
	int32 ServerWeatherRandomInteger(int32 Min, int32 Max) const;
	UFUNCTION(BlueprintCallable, Category = "Weather|Network")
	void SetNetworkSunVisible();
	UFUNCTION(BlueprintCallable, Category = "Weather|Network")
	void SetNetworkWeatherTime(int32 Hour, int32 Minute, uint8 Weather);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient) FSWWeatherPlaybackWindow FutureVoyageWindow;
	int32 RestoredVoyageGeneration = INDEX_NONE;
	int32 InitialVoyageHour = 0;
	int32 InitialVoyageMinute = 0;
	uint8 InitialVoyageWeather = 0;
	FSWWeatherWindState InitialVoyageWind;
	bool bHasInitialVoyageSettings = false;
	bool bInitializingVoyageWeather = false;
	UPROPERTY(ReplicatedUsing = OnRep_PlaybackWindow) FSWWeatherPlaybackWindow PlaybackWindow;
	UPROPERTY(Transient) TArray<FSWWeatherPlaybackSegment> PlaybackBuffer;
	UPROPERTY(Transient) TArray<FSWWeatherPlaybackSegment> PendingEpochBuffer;
	UPROPERTY(Transient) TArray<FSWWeatherPlaybackSegment> ServerHistory;
	UPROPERTY(Transient) FSWWeatherPlaybackSegment ActiveWeatherState;
	UPROPERTY(Transient) FSWWeatherPlaybackSegment PlannerState;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> PlannerMaterials;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> RecoveryMaterials;
	UPROPERTY(Transient) TMap<FString, TObjectPtr<UObject>> ReadyAssets;
	UPROPERTY(Transient) FSWWeatherPlaybackSegment RecoveryTarget;
	UPROPERTY(Transient) FSWWeatherPlaybackSegment RecoveryNext;
	UPROPERTY(Transient) FSWWeatherVisualFrame RecoverySourceFrame;
	UPROPERTY(Transient) FSWWeatherVisualFrame RecoveryTargetFrame;
	UPROPERTY(Transient) FSWWeatherVisualFrame LastVisualFrame;
	UPROPERTY(Replicated) FSWWeatherWindState WindState;
	UPROPERTY() TObjectPtr<UMaterialParameterCollection> SkyParameters;
	UFUNCTION() void OnRep_PlaybackWindow();
	bool ValidateAdapter() const;
	bool Invoke(FName FunctionName, const TMap<FName, double>& Arguments = {});
	double ReadNumber(FName Name) const;
	void WriteNumber(FName Name, double Value);
	UObject* ReadObject(FName Name) const;
	bool CaptureSegment(FSWWeatherPlaybackSegment& Segment);
	bool RestoreSegmentValues(const FSWWeatherPlaybackSegment& Segment);
	bool ValidatePropertyValues(const FSWWeatherPlaybackSegment& Segment, bool bApply);
	void BuildFutureSegments(double Now);
	void PublishPlaybackWindow(double Now);
	void MergePlaybackWindow(const FSWWeatherPlaybackWindow& Window);
	void PrepareSegments();
	bool IsReady(const FSWWeatherPlaybackSegment& Segment) const;
	bool TryStartPlayback(double Now, bool bEpochReset);
	void AdvanceAcrossSegments(double Step);
	void EvaluatePresentedWeather();
	bool EnterSegment(const FSWWeatherPlaybackSegment& Segment);
	bool CaptureVisualFrame(FSWWeatherVisualFrame& Frame);
	bool EvaluateVisualFrame(const FSWWeatherPlaybackSegment& Segment, double Alpha, FSWWeatherVisualFrame& Frame);
	bool TryStartRecovery(double Now);
	void AdvanceRecovery(double Step);
	void CompareVisualFrames(const TCHAR* Phase, const FSWWeatherVisualFrame& From, const FSWWeatherVisualFrame& To);
	void FailGeneration(const TCHAR* Reason);
	void LogOnce(uint32 Epoch, uint32 Sequence, const FString& Reason);
	void PruneReadyAssets();
	void AdvanceWind(double ServerTime);
	FVector ResolveWindOffset(double ServerTime) const;
	double GetServerTime() const;
	void LogWeatherDiagnostics(const TCHAR* Phase, double ServerTime) const;
	void SampleWeatherDiagnostics(double ServerTime, double Alpha);
	double DiagnosticLastServerTime = 0.;
	double DiagnosticLastLocalTime = 0.;
	double DiagnosticLastAlpha = 0.;
	double DiagnosticNextSampleTime = 0.;
	uint32 DiagnosticLastSequence = 0;
	bool bAdapterReady = false;
	bool bEventsReady = false;
	bool bGenerationFailed = false;
	bool bInitialWindowPublished = false;
	bool bHasVisualFrame = false;
	uint32 AppliedEpoch = 0;
	uint32 AppliedSequence = 0;
	uint32 ReceivedEpoch = 0;
	double ReceivedPublishedTime = 0.;
	double EpochPlaybackStartServerTime = 0.;
	double ServerEpochStartTime = 0.;
	double PlaybackServerTime = 0.;
	double LastLocalWorldTime = 0.;
	double PlaybackRate = 1.;
	double LastPresentedAlpha = 0.;
	double LocalElapsed = 0.;
	double PlaybackStep = 0.;
	double RecoveryElapsed = 0.;
	double NextClockSampleTime = 0.;
	double ClockSampleStartTime = 0.;
	double NextClockWarningTime = 0.;
	double NextClockErrorTime = 0.;
	ESWWeatherPlaybackStatus PlaybackStatus = ESWWeatherPlaybackStatus::WaitingInitialState;
	TArray<TPair<double, double>> ClockSamples;
	TMap<uint64, ESWWeatherSegmentReadiness> SegmentReadiness;
	TMap<FString, TSharedPtr<FStreamableHandle>> AssetLoadHandles;
	TSet<FString> CompletedAssetLoads;
	TSet<FString> FailedAssetPaths;
	TSet<FString> ReportedErrors;
	double NextWindServerTime = 0.;
	float PendingVisibilityFrom = 1.f;
};
