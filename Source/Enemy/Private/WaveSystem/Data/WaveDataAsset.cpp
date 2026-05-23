
#include "WaveSystem/Data/WaveDataAsset.h"

#include "BaseEnemy.h"


DEFINE_LOG_CATEGORY_STATIC(LogWaveData, Log, All);

int32 UWaveDataAsset::GetWaveCount() const
{
    return WaveDefinitions.Num();
}

bool UWaveDataAsset::IsValidWaveIndex(int32 WaveArrayIndex) const
{
    return WaveDefinitions.IsValidIndex(WaveArrayIndex);
}

const FWaveDefinition& UWaveDataAsset::GetWaveDefinitionChecked(int32 WaveArrayIndex) const
{
    check(WaveDefinitions.IsValidIndex(WaveArrayIndex));
    return WaveDefinitions[WaveArrayIndex];
}

bool UWaveDataAsset::GetWaveDefinition(int32 WaveArrayIndex, FWaveDefinition& OutWaveDefinition) const
{
    if (!IsValidWaveIndex(WaveArrayIndex))
    {
        return false;
    }

    OutWaveDefinition = WaveDefinitions[WaveArrayIndex];
    return true;
}

bool UWaveDataAsset::FindWaveIndexByDisplayNumber(int32 DisplayWaveNumber, int32& OutWaveArrayIndex) const
{
    OutWaveArrayIndex = INDEX_NONE;

    for (int32 WaveIndex = 0; WaveIndex < WaveDefinitions.Num(); ++WaveIndex)
    {
        if (WaveDefinitions[WaveIndex].DisplayWaveNumber == DisplayWaveNumber)
        {
            OutWaveArrayIndex = WaveIndex;
            return true;
        }
    }

    return false;
}

bool UWaveDataAsset::GetWaveDefinitionByDisplayNumber(
    int32 DisplayWaveNumber,
    FWaveDefinition& OutWaveDefinition
) const
{
    int32 FoundWaveArrayIndex = INDEX_NONE;

    if (!FindWaveIndexByDisplayNumber(DisplayWaveNumber, FoundWaveArrayIndex))
    {
        return false;
    }

    return GetWaveDefinition(FoundWaveArrayIndex, OutWaveDefinition);
}

bool UWaveDataAsset::ValidateWaveData(TArray<FString>& OutErrors) const
{
    OutErrors.Reset();

    if (WaveDefinitions.Num() <= 0)
    {
        AddValidationError(
            OutErrors,
            TEXT("[WaveData] WaveDefinitions is empty.")
        );
        return false;
    }

    TSet<int32> UsedDisplayWaveNumbers;

    for (int32 WaveIndex = 0; WaveIndex < WaveDefinitions.Num(); ++WaveIndex)
    {
        const FWaveDefinition& WaveDefinition = WaveDefinitions[WaveIndex];

        if (WaveDefinition.DisplayWaveNumber <= 0)
        {
            AddValidationError(
                OutErrors,
                FString::Printf(
                    TEXT("[WaveData] Invalid DisplayWaveNumber. WaveArrayIndex=%d, DisplayWaveNumber=%d"),
                    WaveIndex,
                    WaveDefinition.DisplayWaveNumber
                )
            );
        }

        if (UsedDisplayWaveNumbers.Contains(WaveDefinition.DisplayWaveNumber))
        {
            AddValidationError(
                OutErrors,
                FString::Printf(
                    TEXT("[WaveData] Duplicate DisplayWaveNumber detected. WaveArrayIndex=%d, DisplayWaveNumber=%d"),
                    WaveIndex,
                    WaveDefinition.DisplayWaveNumber
                )
            );
        }
        else
        {
            UsedDisplayWaveNumbers.Add(WaveDefinition.DisplayWaveNumber);
        }

        if (WaveDefinition.PreWaveDelay < 0.0f)
        {
            AddValidationError(
                OutErrors,
                FString::Printf(
                    TEXT("[WaveData] PreWaveDelay cannot be negative. WaveArrayIndex=%d, PreWaveDelay=%.2f"),
                    WaveIndex,
                    WaveDefinition.PreWaveDelay
                )
            );
        }

        if (WaveDefinition.NextWaveDelay < 0.0f)
        {
            AddValidationError(
                OutErrors,
                FString::Printf(
                    TEXT("[WaveData] NextWaveDelay cannot be negative. WaveArrayIndex=%d, NextWaveDelay=%.2f"),
                    WaveIndex,
                    WaveDefinition.NextWaveDelay
                )
            );
        }

        if (WaveDefinition.SpawnGroups.Num() <= 0)
        {
            AddValidationError(
                OutErrors,
                FString::Printf(
                    TEXT("[WaveData] SpawnGroups is empty. WaveArrayIndex=%d, DisplayWaveNumber=%d"),
                    WaveIndex,
                    WaveDefinition.DisplayWaveNumber
                )
            );

            continue;
        }

        for (int32 GroupIndex = 0; GroupIndex < WaveDefinition.SpawnGroups.Num(); ++GroupIndex)
        {
            const FSpawnGroupDefinition& GroupDefinition = WaveDefinition.SpawnGroups[GroupIndex];

            if (!GroupDefinition.EnemyClass)
            {
                AddValidationError(
                    OutErrors,
                    FString::Printf(
                        TEXT("[WaveData] EnemyClass is null. WaveArrayIndex=%d, GroupIndex=%d"),
                        WaveIndex,
                        GroupIndex
                    )
                );
            }

            if (GroupDefinition.Count <= 0)
            {
                AddValidationError(
                    OutErrors,
                    FString::Printf(
                        TEXT("[WaveData] Count must be greater than 0. WaveArrayIndex=%d, GroupIndex=%d, Count=%d"),
                        WaveIndex,
                        GroupIndex,
                        GroupDefinition.Count
                    )
                );
            }

            if (GroupDefinition.RouteId.IsNone())
            {
                AddValidationError(
                    OutErrors,
                    FString::Printf(
                        TEXT("[WaveData] RouteId is None. WaveArrayIndex=%d, GroupIndex=%d"),
                        WaveIndex,
                        GroupIndex
                    )
                );
            }

            if (GroupDefinition.StartDelay < 0.0f)
            {
                AddValidationError(
                    OutErrors,
                    FString::Printf(
                        TEXT("[WaveData] StartDelay cannot be negative. WaveArrayIndex=%d, GroupIndex=%d, StartDelay=%.2f"),
                        WaveIndex,
                        GroupIndex,
                        GroupDefinition.StartDelay
                    )
                );
            }

            if (GroupDefinition.SpawnInterval < 0.0f)
            {
                AddValidationError(
                    OutErrors,
                    FString::Printf(
                        TEXT("[WaveData] SpawnInterval cannot be negative. WaveArrayIndex=%d, GroupIndex=%d, SpawnInterval=%.2f"),
                        WaveIndex,
                        GroupIndex,
                        GroupDefinition.SpawnInterval
                    )
                );
            }

            if (GroupDefinition.BurstCount <= 0)
            {
                AddValidationError(
                    OutErrors,
                    FString::Printf(
                        TEXT("[WaveData] BurstCount must be greater than 0. WaveArrayIndex=%d, GroupIndex=%d, BurstCount=%d"),
                        WaveIndex,
                        GroupIndex,
                        GroupDefinition.BurstCount
                    )
                );
            }

            if (GroupDefinition.SpawnRadius < 0.0f)
            {
                AddValidationError(
                    OutErrors,
                    FString::Printf(
                        TEXT("[WaveData] SpawnRadius cannot be negative. WaveArrayIndex=%d, GroupIndex=%d, SpawnRadius=%.2f"),
                        WaveIndex,
                        GroupIndex,
                        GroupDefinition.SpawnRadius
                    )
                );
            }

            if (GroupDefinition.HealthMultiplier <= 0.0f)
            {
                AddValidationError(
                    OutErrors,
                    FString::Printf(
                        TEXT("[WaveData] HealthMultiplier must be greater than 0. WaveArrayIndex=%d, GroupIndex=%d, HealthMultiplier=%.2f"),
                        WaveIndex,
                        GroupIndex,
                        GroupDefinition.HealthMultiplier
                    )
                );
            }

            if (GroupDefinition.SpeedMultiplier <= 0.0f)
            {
                AddValidationError(
                    OutErrors,
                    FString::Printf(
                        TEXT("[WaveData] SpeedMultiplier must be greater than 0. WaveArrayIndex=%d, GroupIndex=%d, SpeedMultiplier=%.2f"),
                        WaveIndex,
                        GroupIndex,
                        GroupDefinition.SpeedMultiplier
                    )
                );
            }

            if (GroupDefinition.EnemyLevel <= 0)
            {
                AddValidationError(
                    OutErrors,
                    FString::Printf(
                        TEXT("[WaveData] EnemyLevel must be greater than 0. WaveArrayIndex=%d, GroupIndex=%d, EnemyLevel=%d"),
                        WaveIndex,
                        GroupIndex,
                        GroupDefinition.EnemyLevel
                    )
                );
            }

            if (GroupDefinition.BurstCount > GroupDefinition.Count)
            {
                UE_LOG(
                    LogWaveData,
                    Warning,
                    TEXT("[WaveData] BurstCount is greater than Count. This is allowed, but only Count enemies will be spawned. WaveArrayIndex=%d, GroupIndex=%d, Count=%d, BurstCount=%d"),
                    WaveIndex,
                    GroupIndex,
                    GroupDefinition.Count,
                    GroupDefinition.BurstCount
                );
            }

            if (GroupDefinition.SpawnInterval <= 0.0f && GroupDefinition.Count > GroupDefinition.BurstCount)
            {
                UE_LOG(
                    LogWaveData,
                    Warning,
                    TEXT("[WaveData] SpawnInterval is 0 while multiple spawn batches are required. This may spawn enemies very quickly. WaveArrayIndex=%d, GroupIndex=%d, Count=%d, BurstCount=%d"),
                    WaveIndex,
                    GroupIndex,
                    GroupDefinition.Count,
                    GroupDefinition.BurstCount
                );
            }
        }
    }

    return OutErrors.Num() == 0;
}

bool UWaveDataAsset::ValidateWaveDataAndLog() const
{
    TArray<FString> Errors;
    const bool bIsValid = ValidateWaveData(Errors);

    if (bIsValid)
    {
        UE_LOG(
            LogWaveData,
            Log,
            TEXT("[WaveData] Validation succeeded. Asset=%s, WaveCount=%d"),
            *GetNameSafe(this),
            WaveDefinitions.Num()
        );

        return true;
    }

    UE_LOG(
        LogWaveData,
        Error,
        TEXT("[WaveData] Validation failed. Asset=%s, ErrorCount=%d"),
        *GetNameSafe(this),
        Errors.Num()
    );

    for (const FString& Error : Errors)
    {
        UE_LOG(LogWaveData, Error, TEXT("%s"), *Error);
    }

    return false;
}

void UWaveDataAsset::AddValidationError(TArray<FString>& OutErrors, const FString& Error) const
{
    OutErrors.Add(Error);
}