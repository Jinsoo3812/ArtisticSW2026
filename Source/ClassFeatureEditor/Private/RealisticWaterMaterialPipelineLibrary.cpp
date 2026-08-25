#include "RealisticWaterMaterialPipelineLibrary.h"

#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialParameterCollection.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "SWCabinWaterCullComponent.h"
#include "SWPersistentFoamField.h"

bool URealisticWaterMaterialPipelineLibrary::AddCabinWaterCullComponentToBlueprint(UBlueprint* Blueprint)
{
	if (!IsValid(Blueprint) || !IsValid(Blueprint->SimpleConstructionScript))
	{
		return false;
	}

	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (IsValid(Node) && Node->ComponentClass &&
			Node->ComponentClass->IsChildOf(USWCabinWaterCullComponent::StaticClass()))
		{
			return true;
		}
	}

	Blueprint->Modify();
	USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(
		USWCabinWaterCullComponent::StaticClass(), TEXT("CabinWaterCull"));
	if (!IsValid(Node))
	{
		return false;
	}
	Blueprint->SimpleConstructionScript->AddNode(Node);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	Blueprint->MarkPackageDirty();
	return true;
}

ASWPersistentFoamField* URealisticWaterMaterialPipelineLibrary::SpawnPersistentFoamFieldDirect(
	FVector Location,
	FRotator Rotation)
{
	if (!GEditor)
	{
		return nullptr;
	}

	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	if (!IsValid(EditorWorld) || !IsValid(EditorWorld->GetCurrentLevel()))
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.OverrideLevel = EditorWorld->GetCurrentLevel();
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags = RF_Transactional;

	ASWPersistentFoamField* FoamField = EditorWorld->SpawnActor<ASWPersistentFoamField>(
		ASWPersistentFoamField::StaticClass(),
		Location,
		Rotation,
		SpawnParameters);
	if (IsValid(FoamField))
	{
		FoamField->Modify();
		EditorWorld->GetCurrentLevel()->MarkPackageDirty();
	}
	return FoamField;
}

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

TArray<UMaterialExpression*> URealisticWaterMaterialPipelineLibrary::GetMaterialFunctionExpressions(
	UMaterialFunction* Function)
{
	if (!IsValid(Function))
	{
		return {};
	}

	TArray<UMaterialExpression*> Result;
	for (UMaterialExpression* Expression : Function->GetExpressions())
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

TArray<FName> URealisticWaterMaterialPipelineLibrary::GetMaterialExpressionOutputNames(
	UMaterialExpression* Expression)
{
	TArray<FName> Result;
	if (!IsValid(Expression))
	{
		return Result;
	}

	for (const FExpressionOutput& Output : Expression->GetOutputs())
	{
		Result.Add(Output.OutputName);
	}
	return Result;
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

bool URealisticWaterMaterialPipelineLibrary::ConfigureFoamWaterAttributeOverride(
	UMaterialExpressionSetMaterialAttributes* SetAttributes,
	UMaterialExpression* BaseColorExpression,
	UMaterialExpression* RoughnessExpression,
	UMaterialExpression* SpecularExpression,
	UMaterialExpression* EmissiveExpression)
{
	if (!IsValid(SetAttributes) || !IsValid(BaseColorExpression) ||
		!IsValid(RoughnessExpression) || !IsValid(SpecularExpression) ||
		!IsValid(EmissiveExpression))
	{
		return false;
	}

	SetAttributes->Modify();
	const bool bBaseColorConnected = SetAttributes->ConnectInputAttribute(MP_BaseColor, BaseColorExpression);
	const bool bRoughnessConnected = SetAttributes->ConnectInputAttribute(MP_Roughness, RoughnessExpression);
	const bool bSpecularConnected = SetAttributes->ConnectInputAttribute(MP_Specular, SpecularExpression);
	const bool bEmissiveConnected = SetAttributes->ConnectInputAttribute(MP_EmissiveColor, EmissiveExpression);
	SetAttributes->PostEditChange();
	return bBaseColorConnected && bRoughnessConnected && bSpecularConnected && bEmissiveConnected;
}

bool URealisticWaterMaterialPipelineLibrary::ConfigureGerstnerFoamAttributeOverride(
	UMaterialExpressionSetMaterialAttributes* SetAttributes,
	UMaterialExpression* FoamSurfaceExpression,
	UMaterialExpression* EmissiveExpression)
{
	if (!IsValid(SetAttributes) || !IsValid(FoamSurfaceExpression) ||
		!IsValid(EmissiveExpression))
	{
		return false;
	}

	SetAttributes->Modify();
	const bool bBaseColorConnected = SetAttributes->ConnectInputAttribute(
		MP_BaseColor, FoamSurfaceExpression, 0);
	const bool bRoughnessConnected = SetAttributes->ConnectInputAttribute(
		MP_Roughness, FoamSurfaceExpression, 2);
	const bool bOpacityConnected = SetAttributes->ConnectInputAttribute(
		MP_Opacity, FoamSurfaceExpression, 1);
	const bool bEmissiveConnected = SetAttributes->ConnectInputAttribute(
		MP_EmissiveColor, EmissiveExpression);
	SetAttributes->PostEditChange();
	return bBaseColorConnected && bRoughnessConnected &&
		bOpacityConnected && bEmissiveConnected;
}

bool URealisticWaterMaterialPipelineLibrary::ConnectEmissiveAttribute(
	UMaterialExpressionSetMaterialAttributes* SetAttributes,
	UMaterialExpression* EmissiveExpression)
{
	if (!IsValid(SetAttributes) || !IsValid(EmissiveExpression))
	{
		return false;
	}

	SetAttributes->Modify();
	const bool bConnected = SetAttributes->ConnectInputAttribute(
		MP_EmissiveColor, EmissiveExpression);
	SetAttributes->PostEditChange();
	return bConnected;
}

bool URealisticWaterMaterialPipelineLibrary::ConnectOpacityMaskAttribute(
	UMaterialExpressionSetMaterialAttributes* SetAttributes,
	UMaterialExpression* OpacityMaskExpression)
{
	if (!IsValid(SetAttributes) || !IsValid(OpacityMaskExpression))
	{
		return false;
	}
	SetAttributes->Modify();
	const bool bConnected = SetAttributes->ConnectInputAttribute(
		MP_OpacityMask, OpacityMaskExpression);
	SetAttributes->PostEditChange();
	return bConnected;
}

bool URealisticWaterMaterialPipelineLibrary::ConfigureCabinWaterCullCollection(
	UMaterialParameterCollection* Collection)
{
	if (!IsValid(Collection))
	{
		return false;
	}
	Collection->Modify();
	auto AddScalar = [Collection](FName Name, float DefaultValue)
	{
		const bool bExists = Collection->ScalarParameters.ContainsByPredicate(
			[Name](const FCollectionScalarParameter& Parameter)
			{
				return Parameter.ParameterName == Name;
			});
		if (!bExists)
		{
			FCollectionScalarParameter Parameter;
			Parameter.ParameterName = Name;
			Parameter.DefaultValue = DefaultValue;
			Collection->ScalarParameters.Add(Parameter);
		}
	};
	auto AddVector = [Collection](FName Name, const FLinearColor& DefaultValue)
	{
		const bool bExists = Collection->VectorParameters.ContainsByPredicate(
			[Name](const FCollectionVectorParameter& Parameter)
			{
				return Parameter.ParameterName == Name;
			});
		if (!bExists)
		{
			FCollectionVectorParameter Parameter;
			Parameter.ParameterName = Name;
			Parameter.DefaultValue = DefaultValue;
			Collection->VectorParameters.Add(Parameter);
		}
	};
	AddScalar(TEXT("SW_CabinCullEnabled"), 0.0f);
	AddScalar(TEXT("SW_CabinCullThreshold"), 0.35f);
	AddScalar(TEXT("SW_CabinCullDebugView"), 0.0f);
	AddVector(TEXT("SW_CabinCullInvRow0"), FLinearColor(1, 0, 0, 0));
	AddVector(TEXT("SW_CabinCullInvRow1"), FLinearColor(0, 1, 0, 0));
	AddVector(TEXT("SW_CabinCullInvRow2"), FLinearColor(0, 0, 1, 0));
	AddVector(TEXT("SW_CabinCullLocalMin"), FLinearColor::Black);
	AddVector(TEXT("SW_CabinCullLocalMax"), FLinearColor::Black);
	Collection->PostEditChange();
	Collection->MarkPackageDirty();
	return true;
}

bool URealisticWaterMaterialPipelineLibrary::ConfigureCollectionParameterExpression(
	UMaterialExpressionCollectionParameter* Expression,
	UMaterialParameterCollection* Collection,
	FName ParameterName)
{
	if (!IsValid(Expression) || !IsValid(Collection) || ParameterName.IsNone())
	{
		return false;
	}
	const FGuid ParameterId = Collection->GetParameterId(ParameterName);
	if (!ParameterId.IsValid())
	{
		return false;
	}
	Expression->Modify();
	Expression->Collection = Collection;
	Expression->ParameterName = ParameterName;
	Expression->ParameterId = ParameterId;
	Expression->ExpressionGUID = FGuid::NewGuid();
	Expression->PostEditChange();
	return true;
}

bool URealisticWaterMaterialPipelineLibrary::SetCabinWaterCullBoundsDefaults(
	UMaterialParameterCollection* Collection,
	FVector LocalMin,
	FVector LocalMax)
{
	if (!IsValid(Collection))
	{
		return false;
	}
	Collection->Modify();
	bool bSetMin = false;
	bool bSetMax = false;
	for (FCollectionVectorParameter& Parameter : Collection->VectorParameters)
	{
		if (Parameter.ParameterName == TEXT("SW_CabinCullLocalMin"))
		{
			Parameter.DefaultValue = FLinearColor(LocalMin.X, LocalMin.Y, LocalMin.Z, 0.0f);
			bSetMin = true;
		}
		else if (Parameter.ParameterName == TEXT("SW_CabinCullLocalMax"))
		{
			Parameter.DefaultValue = FLinearColor(LocalMax.X, LocalMax.Y, LocalMax.Z, 0.0f);
			bSetMax = true;
		}
	}
	if (bSetMin && bSetMax)
	{
		Collection->PostEditChange();
		Collection->MarkPackageDirty();
	}
	return bSetMin && bSetMax;
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

bool URealisticWaterMaterialPipelineLibrary::ConfigureFloat3CustomExpression(
	UMaterialExpressionCustom* CustomExpression,
	const TArray<FName>& InputNames,
	const FString& Code,
	const FString& Description)
{
	return ConfigureTypedCustomExpression(
		CustomExpression, InputNames, Code, Description, CMOT_Float3);
}

bool URealisticWaterMaterialPipelineLibrary::ConfigureFloat3CustomExpressionWithIncludes(
	UMaterialExpressionCustom* CustomExpression,
	const TArray<FName>& InputNames,
	const FString& Code,
	const FString& Description,
	const TArray<FString>& IncludeFilePaths)
{
	if (!ConfigureTypedCustomExpression(
		CustomExpression, InputNames, Code, Description, CMOT_Float3))
	{
		return false;
	}

	CustomExpression->Modify();
	CustomExpression->IncludeFilePaths = IncludeFilePaths;
	CustomExpression->PostEditChange();
	return !CustomExpression->IncludeFilePaths.IsEmpty();
}

bool URealisticWaterMaterialPipelineLibrary::ConfigureWaveHeightOpticsCustomExpression(
	UMaterialExpressionCustom* CustomExpression,
	const TArray<FName>& InputNames,
	const FString& Code,
	const FString& Description,
	const TArray<FString>& IncludeFilePaths)
{
	if (!ConfigureTypedCustomExpression(
		CustomExpression, InputNames, Code, Description, CMOT_Float3))
	{
		return false;
	}

	CustomExpression->Modify();
	CustomExpression->IncludeFilePaths = IncludeFilePaths;
	CustomExpression->AdditionalOutputs.Reset(3);

	FCustomOutput& ScatteringA = CustomExpression->AdditionalOutputs.AddDefaulted_GetRef();
	ScatteringA.OutputName = TEXT("ScatteringA");
	ScatteringA.OutputType = CMOT_Float1;

	FCustomOutput& AbsorptionRGB = CustomExpression->AdditionalOutputs.AddDefaulted_GetRef();
	AbsorptionRGB.OutputName = TEXT("AbsorptionRGB");
	AbsorptionRGB.OutputType = CMOT_Float3;

	FCustomOutput& AbsorptionA = CustomExpression->AdditionalOutputs.AddDefaulted_GetRef();
	AbsorptionA.OutputName = TEXT("AbsorptionA");
	AbsorptionA.OutputType = CMOT_Float1;

	CustomExpression->RebuildOutputs();
	CustomExpression->PostEditChange();
	return CustomExpression->AdditionalOutputs.Num() == 3
		&& !CustomExpression->IncludeFilePaths.IsEmpty();
}

bool URealisticWaterMaterialPipelineLibrary::ConfigureGerstnerFoamSurfaceCustomExpression(
	UMaterialExpressionCustom* CustomExpression,
	const TArray<FName>& InputNames,
	const FString& Code,
	const FString& Description,
	const TArray<FString>& IncludeFilePaths)
{
	if (!ConfigureTypedCustomExpression(
		CustomExpression, InputNames, Code, Description, CMOT_Float3))
	{
		return false;
	}

	CustomExpression->Modify();
	CustomExpression->IncludeFilePaths = IncludeFilePaths;
	CustomExpression->AdditionalOutputs.Reset(2);

	FCustomOutput& FoamOpacity = CustomExpression->AdditionalOutputs.AddDefaulted_GetRef();
	FoamOpacity.OutputName = TEXT("FoamOpacity");
	FoamOpacity.OutputType = CMOT_Float1;

	FCustomOutput& FoamRoughness = CustomExpression->AdditionalOutputs.AddDefaulted_GetRef();
	FoamRoughness.OutputName = TEXT("FoamRoughness");
	FoamRoughness.OutputType = CMOT_Float1;

	CustomExpression->RebuildOutputs();
	CustomExpression->PostEditChange();
	return CustomExpression->AdditionalOutputs.Num() == 2
		&& !CustomExpression->IncludeFilePaths.IsEmpty();
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

bool URealisticWaterMaterialPipelineLibrary::ConfigureFloat1CustomExpressionWithIncludes(
	UMaterialExpressionCustom* CustomExpression,
	const TArray<FName>& InputNames,
	const FString& Code,
	const FString& Description,
	const TArray<FString>& IncludeFilePaths)
{
	if (!ConfigureTypedCustomExpression(
		CustomExpression, InputNames, Code, Description, CMOT_Float1))
	{
		return false;
	}

	CustomExpression->Modify();
	CustomExpression->IncludeFilePaths = IncludeFilePaths;
	CustomExpression->PostEditChange();
	return !CustomExpression->IncludeFilePaths.IsEmpty();
}
