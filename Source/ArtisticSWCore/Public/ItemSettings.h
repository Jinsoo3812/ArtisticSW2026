// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ItemData.h"
#include "ItemSettings.generated.h"

class UDataTable;
class UItemData;
/**
 * ItemSubsystem에 캐싱할 에셋을 설정하는 Developer Setting
 */
UCLASS(Config = Game, defaultconfig, meta = (DisplayName = "Item System Settings"))
class ARTISTICSWCORE_API UItemSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 기획 수치가 담긴 CSV 데이터 테이블 경로
	UPROPERTY(Config, EditAnywhere, Category = "Item Data", meta = (AllowedClasses = "/Script/Engine.DataTable"))
	TSoftObjectPtr<UDataTable> ItemFeatureDataTable;

	// 에셋 매핑이 담긴 DA 카탈로그 경로
	UPROPERTY(Config, EditAnywhere, Category = "Item Data")
	TSoftObjectPtr<UItemData> ItemAssetRegistry;
};
