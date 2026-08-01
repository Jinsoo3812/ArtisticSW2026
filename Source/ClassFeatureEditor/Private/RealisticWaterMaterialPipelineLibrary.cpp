#include "RealisticWaterMaterialPipelineLibrary.h"

#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/Material.h"

TArray<UMaterialExpression*> URealisticWaterMaterialPipelineLibrary::GetMaterialExpressions(
	UMaterial* Material)
{
	if (!IsValid(Material))
	{
		return {};
	}

	TArray<UMaterialExpression*> Result;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (IsValid(Expression))
		{
			Result.Add(Expression);
		}
	}
	return Result;
}

UMaterialExpression* URealisticWaterMaterialPipelineLibrary::GetConnectedInputExpression(
	UMaterialExpression* Expression,
	int32 InputIndex)
{
	if (!IsValid(Expression) || InputIndex < 0)
	{
		return nullptr;
	}

	FExpressionInput* Input = Expression->GetInput(InputIndex);
	return Input ? Input->Expression : nullptr;
}

bool URealisticWaterMaterialPipelineLibrary::ConfigureAttributeOverride(
	UMaterialExpressionSetMaterialAttributes* SetAttributes,
	UMaterialExpression* NormalExpression,
	UMaterialExpression* RoughnessExpression)
{
	if (!IsValid(SetAttributes) || !IsValid(NormalExpression) || !IsValid(RoughnessExpression))
	{
		return false;
	}

	SetAttributes->Modify();
	const bool bNormalConnected = SetAttributes->ConnectInputAttribute(MP_Normal, NormalExpression);
	const bool bRoughnessConnected = SetAttributes->ConnectInputAttribute(MP_Roughness, RoughnessExpression);
	SetAttributes->PostEditChange();
	return bNormalConnected && bRoughnessConnected;
}

bool URealisticWaterMaterialPipelineLibrary::ConfigureVisualWaterAttributeOverride(
	UMaterialExpressionSetMaterialAttributes* SetAttributes,
	UMaterialExpression* NormalExpression,
	UMaterialExpression* RoughnessExpression,
	UMaterialExpression* SpecularExpression)
{
	if (!IsValid(SetAttributes) || !IsValid(NormalExpression) ||
		!IsValid(RoughnessExpression) || !IsValid(SpecularExpression))
	{
		return false;
	}

	SetAttributes->Modify();
	const bool bNormalConnected = SetAttributes->ConnectInputAttribute(MP_Normal, NormalExpression);
	const bool bRoughnessConnected = SetAttributes->ConnectInputAttribute(MP_Roughness, RoughnessExpression);
	const bool bSpecularConnected = SetAttributes->ConnectInputAttribute(MP_Specular, SpecularExpression);
	SetAttributes->PostEditChange();
	return bNormalConnected && bRoughnessConnected && bSpecularConnected;
}

int32 URealisticWaterMaterialPipelineLibrary::ConfigureLinearGrayscaleSampler(
	UMaterial* Material,
	FName TextureParameterName)
{
	if (!IsValid(Material) || TextureParameterName.IsNone())
	{
		return 0;
	}

	int32 ChangedCount = 0;
	Material->Modify();
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		UMaterialExpressionTextureSampleParameter2D* TextureParameter =
			Cast<UMaterialExpressionTextureSampleParameter2D>(Expression);
		if (!IsValid(TextureParameter) || TextureParameter->ParameterName != TextureParameterName)
		{
			continue;
		}

		TextureParameter->Modify();
		TextureParameter->SamplerType = SAMPLERTYPE_LinearGrayscale;
		TextureParameter->PostEditChange();
		++ChangedCount;
	}
	Material->PostEditChange();
	return ChangedCount;
}

int32 URealisticWaterMaterialPipelineLibrary::SetScalarParameterDefault(
	UMaterial* Material,
	FName ParameterName,
	float DefaultValue)
{
	if (!IsValid(Material) || ParameterName.IsNone())
	{
		return 0;
	}

	int32 ChangedCount = 0;
	Material->Modify();
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		UMaterialExpressionScalarParameter* ScalarParameter =
			Cast<UMaterialExpressionScalarParameter>(Expression);
		if (!IsValid(ScalarParameter) || ScalarParameter->ParameterName != ParameterName)
		{
			continue;
		}

		ScalarParameter->Modify();
		ScalarParameter->DefaultValue = DefaultValue;
		ScalarParameter->PostEditChange();
		++ChangedCount;
	}
	Material->PostEditChange();
	return ChangedCount;
}

int32 URealisticWaterMaterialPipelineLibrary::InitializeMissingParameterGuids(UMaterial* Material)
{
	if (!IsValid(Material))
	{
		return 0;
	}

	int32 ChangedCount = 0;
	Material->Modify();
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (!IsValid(Expression) || !Expression->HasAParameterName())
		{
			continue;
		}

		FGuid& ParameterGuid = Expression->GetParameterExpressionId();
		if (ParameterGuid.IsValid())
		{
			continue;
		}

		Expression->Modify();
		Expression->UpdateParameterGuid(true, true);
		++ChangedCount;
	}
	Material->PostEditChange();
	return ChangedCount;
}

bool URealisticWaterMaterialPipelineLibrary::ConfigureFloat4CustomExpression(
	UMaterialExpressionCustom* CustomExpression,
	const TArray<FName>& InputNames,
	const FString& Code,
	const FString& Description)
{
	if (!IsValid(CustomExpression) || InputNames.IsEmpty() || Code.IsEmpty())
	{
		return false;
	}

	CustomExpression->Modify();
	CustomExpression->Inputs.Reset(InputNames.Num());
	for (const FName InputName : InputNames)
	{
		FCustomInput& Input = CustomExpression->Inputs.AddDefaulted_GetRef();
		Input.InputName = InputName;
	}
	CustomExpression->Code = Code;
	CustomExpression->Description = Description;
	CustomExpression->OutputType = CMOT_Float4;
	CustomExpression->RebuildOutputs();
	CustomExpression->PostEditChange();
	return CustomExpression->Inputs.Num() == InputNames.Num();
}

namespace
{
	bool ConfigureTypedCustomExpression(
		UMaterialExpressionCustom* CustomExpression,
		const TArray<FName>& InputNames,
		const FString& Code,
		const FString& Description,
		ECustomMaterialOutputType OutputType)
	{
		if (!IsValid(CustomExpression) || InputNames.IsEmpty() || Code.IsEmpty())
		{
			return false;
		}

		CustomExpression->Modify();
		CustomExpression->Inputs.Reset(InputNames.Num());
		for (const FName InputName : InputNames)
		{
			FCustomInput& Input = CustomExpression->Inputs.AddDefaulted_GetRef();
			Input.InputName = InputName;
		}
		CustomExpression->Code = Code;
		CustomExpression->Description = Description;
		CustomExpression->OutputType = OutputType;
		CustomExpression->RebuildOutputs();
		CustomExpression->PostEditChange();
		return CustomExpression->Inputs.Num() == InputNames.Num();
	}
}

bool URealisticWaterMaterialPipelineLibrary::ConfigureFloat2CustomExpression(
	UMaterialExpressionCustom* CustomExpression,
	const TArray<FName>& InputNames,
	const FString& Code,
	const FString& Description)
{
	return ConfigureTypedCustomExpression(
		CustomExpression, InputNames, Code, Description, CMOT_Float2);
}

bool URealisticWaterMaterialPipelineLibrary::ConfigureFloat1CustomExpression(
	UMaterialExpressionCustom* CustomExpression,
	const TArray<FName>& InputNames,
	const FString& Code,
	const FString& Description)
{
	return ConfigureTypedCustomExpression(
		CustomExpression, InputNames, Code, Description, CMOT_Float1);
}
