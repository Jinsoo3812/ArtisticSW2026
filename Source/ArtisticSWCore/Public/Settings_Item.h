// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ItemData.h"
#include "Settings_Item.generated.h"

class UDataTable;
class UItemData;

/**
 * 
 */
UCLASS(Config = Game, defaultconfig, meta = (DisplayName = "Item System Settings"))
class ARTISTICSWCORE_API USettings_Item : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	// 기획 수치가 담긴 CSV 데이터 테이블 경로
	UPROPERTY(Config, EditAnywhere, Category = "Item Data", meta = (AllowedClasses = "/Script/Engine.DataTable"))
	TSoftObjectPtr<UDataTable> ItemFeatureDataTable;

	// 에셋 매핑이 담긴 DA 카탈로그 경로
	UPROPERTY(Config, EditAnywhere, Category = "Item Data")
	TSoftObjectPtr<UItemData> ItemAssetRegistry;

	// 아이템 조합식이 담긴 CSV Data Table 경로
	UPROPERTY(Config, EditAnywhere, Category = "Item")
	TSoftObjectPtr<UDataTable> ItemRecipeDataTable;
};
