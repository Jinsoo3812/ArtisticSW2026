// Fill out your copyright notice in the Description page of Project Settings.

#include "ClassFeatureEditorModule.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "RecipeIngredientCustomization.h"

IMPLEMENT_MODULE(FClassFeatureEditorModule, ClassFeatureEditor)

void FClassFeatureEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	PropertyEditor.RegisterCustomPropertyTypeLayout(TEXT("CraftingItemStack"),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FRecipeIngredientCustomization::MakeInstance));
}

void FClassFeatureEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
	{
		FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"))
			.UnregisterCustomPropertyTypeLayout(TEXT("CraftingItemStack"));
	}
}
