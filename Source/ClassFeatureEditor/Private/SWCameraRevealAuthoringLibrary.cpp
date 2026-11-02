#include "SWCameraRevealAuthoringLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionShadowReplace.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialParameterCollection.h"
#include "RHI.h"
#include "ShaderCompiler.h"
#include "UObject/Package.h"

namespace
{
	bool IsOwned(const UObject* Object)
	{
		return Object && (Object->GetPathName().StartsWith(TEXT("/Game/Blueprints/Ship/Materials/CameraReveal/"))
			|| Object->GetPathName().StartsWith(TEXT("/Game/Blueprints/Ship/Materials/Sail/")));
	}
	template<typename T> T* Node(UMaterial* Material, int32 X, int32 Y)
	{
		return CastChecked<T>(UMaterialEditingLibrary::CreateMaterialExpression(Material, T::StaticClass(), X, Y));
	}
}

UMaterialParameterCollection* USWCameraRevealAuthoringLibrary::CreateParameters()
{
	const TCHAR* Path = TEXT("/Game/Blueprints/Ship/Materials/CameraReveal/MPC_CameraReveal");
	UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(nullptr, Path);
	if (!Collection)
	{
		Collection = NewObject<UMaterialParameterCollection>(CreatePackage(Path), TEXT("MPC_CameraReveal"), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Collection);
	}
	Collection->Modify();
	const FName Names[] = {TEXT("RevealCircle"), TEXT("RevealFade"), TEXT("RevealDepth")};
	const FLinearColor Defaults[] = {FLinearColor(0.5f, 0.5f, 0.f, 1.f), FLinearColor(0.f, 0.f, 0.97f, 0.1f), FLinearColor(5.f, 1.f, 0.f, 0.f)};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
	{
		if (!Collection->VectorParameters.ContainsByPredicate([&](const FCollectionVectorParameter& P) { return P.ParameterName == Names[Index]; }))
		{
			FCollectionVectorParameter& Parameter = Collection->VectorParameters.AddDefaulted_GetRef();
			Parameter.ParameterName = Names[Index];
			Parameter.DefaultValue = Defaults[Index];
		}
	}
	Collection->PostEditChange();
	Collection->MarkPackageDirty();
	return Collection;
}

bool USWCameraRevealAuthoringLibrary::AddMaskedReveal(UMaterial* Material, UMaterialParameterCollection* Collection)
{
	if (!IsOwned(Material) || !Collection || Material->bUseMaterialAttributes || Material->MaterialDomain != MD_Surface) return false;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (Expression->Desc == TEXT("SW Camera Reveal v3")) return Material->BlendMode == BLEND_Masked;
	}
	// Usage changes may compile immediately; do this before adding function nodes
	// with new texture references. PostEditChange below rebuilds their cache.
	bool bNeedsRecompile = false;
	Material->SetMaterialUsage(bNeedsRecompile, MATUSAGE_InstancedStaticMeshes);
	UMaterialFunctionInterface* Function = LoadObject<UMaterialFunctionInterface>(nullptr,
		TEXT("/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA.DitherTemporalAA"));
	UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
	if (!Function || !Data) return false;
	Material->Modify();
	const FExpressionInput OriginalMask = Data->OpacityMask;
	UMaterialExpressionCustom* Coverage = Node<UMaterialExpressionCustom>(Material, 1500, 1000);
	Coverage->Desc = TEXT("SW Camera Reveal v3");
	Coverage->Description = Coverage->Desc;
	Coverage->OutputType = CMOT_Float1;
	Coverage->IncludeFilePaths.Add(TEXT("/Project/Shaders/SWCameraReveal.ush"));
	Coverage->Code = TEXT("return SWCameraRevealCoverage(UV, SurfaceDepth, Circle, Fade, Depth);");
	auto AddInput = [Coverage](FName Name, UMaterialExpression* Expression)
	{
		FCustomInput& Input = Coverage->Inputs.AddDefaulted_GetRef();
		Input.InputName = Name;
		Input.Input.Connect(0, Expression);
	};
	AddInput(TEXT("UV"), Node<UMaterialExpressionScreenPosition>(Material, 1000, 800));
	AddInput(TEXT("SurfaceDepth"), Node<UMaterialExpressionPixelDepth>(Material, 1000, 950));
	const FName Parameters[] = {TEXT("RevealCircle"), TEXT("RevealFade"), TEXT("RevealDepth")};
	const FName Inputs[] = {TEXT("Circle"), TEXT("Fade"), TEXT("Depth")};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Parameters); ++Index)
	{
		UMaterialExpressionCollectionParameter* Parameter = Node<UMaterialExpressionCollectionParameter>(Material, 1000, 1100 + Index * 150);
		Parameter->Collection = Collection;
		Parameter->ParameterName = Parameters[Index];
		Parameter->ParameterId = Collection->GetParameterId(Parameters[Index]);
		Parameter->ExpressionGUID = FGuid::NewGuid();
		AddInput(Inputs[Index], Parameter);
	}
	UMaterialExpressionConstant* Zero = Node<UMaterialExpressionConstant>(Material, 1500, 1400);
	Zero->R = 0.f;
	UMaterialExpressionConstant* One = Node<UMaterialExpressionConstant>(Material, 1750, 1400);
	One->R = 1.f;
	UMaterialExpressionMaterialFunctionCall* Dither = Node<UMaterialExpressionMaterialFunctionCall>(Material, 1750, 1000);
	Dither->SetMaterialFunction(Function);
	bool bAlpha = false, bRandom = false;
	for (FFunctionExpressionInput& Input : Dither->FunctionInputs)
	{
		if (!Input.ExpressionInput) continue;
		const FString Name = Input.ExpressionInput->InputName.ToString();
		if (Name.StartsWith(TEXT("Alpha Threshold"))) { Input.Input.Connect(0, Coverage); bAlpha = true; }
		if (Name.StartsWith(TEXT("Random"))) { Input.Input.Connect(0, Zero); bRandom = true; }
	}
	if (!bAlpha || !bRandom) return false;
	UMaterialExpressionMultiply* Mask = Node<UMaterialExpressionMultiply>(Material, 2000, 1000);
	Mask->A.Connect(0, Dither);
	if (OriginalMask.Expression) Mask->B = OriginalMask;
	else Mask->B.Connect(0, One);
	UMaterialExpressionShadowReplace* Shadow = Node<UMaterialExpressionShadowReplace>(Material, 2250, 1000);
	Shadow->Default.Connect(0, Mask);
	if (OriginalMask.Expression) Shadow->Shadow = OriginalMask;
	else Shadow->Shadow.Connect(0, One);
	Data->OpacityMask.Connect(0, Shadow);
	Material->BlendMode = BLEND_Masked;
	Material->OpacityMaskClipValue = FMath::Max(Material->OpacityMaskClipValue, 0.001f);
	Material->PostEditChange();
	Material->MarkPackageDirty();
	UMaterialEditingLibrary::RecompileMaterial(Material);
	return true;
}

bool USWCameraRevealAuthoringLibrary::ConfigureInstance(UMaterialInstanceConstant* Material, float OriginalClipValue)
{
	if (!IsOwned(Material)) return false;
	Material->Modify();
	Material->BasePropertyOverrides.bOverride_BlendMode = true;
	Material->BasePropertyOverrides.BlendMode = BLEND_Masked;
	Material->BasePropertyOverrides.bOverride_OpacityMaskClipValue = true;
	Material->BasePropertyOverrides.OpacityMaskClipValue = FMath::Max(OriginalClipValue, 0.001f);
	Material->PostEditChange();
	Material->MarkPackageDirty();
	return true;
}

FString USWCameraRevealAuthoringLibrary::FinishMaterials(const TArray<UMaterialInterface*>& Materials)
{
	GShaderCompilingManager->FinishAllCompilation();
	for (UMaterialInterface* Material : Materials)
	{
		if (!Material || Material->GetBlendMode() != BLEND_Masked || Material->GetOpacityMaskClipValue() <= 0.f)
			return TEXT("FAILED: expected Masked material with positive clip value");
		const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
		if (!Resource || !Resource->GetGameThreadShaderMap() || !Resource->GetCompileErrors().IsEmpty())
			return TEXT("FAILED: shader compilation: ") + Material->GetPathName();
	}
	return FString::Printf(TEXT("SUCCESS: %d Masked materials; positive clip values"), Materials.Num());
}
