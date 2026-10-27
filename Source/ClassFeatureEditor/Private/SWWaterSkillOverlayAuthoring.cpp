#include "SWWaterSkillOverlaySubsystem.h"

#include "HAL/IConsoleManager.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialParameterCollection.h"
#include "MaterialShared.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RealisticWaterMaterialPipelineLibrary.h"
#include "RHI.h"
#include "ShaderCompiler.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace SWWaterSkillOverlayAuthoring
{
	const TCHAR* OverlayDescription = TEXT("SW Water Skill Surface Overlays v1");

	bool SaveAsset(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Package, Asset, *Filename, Args);
	}

	bool Upgrade(FString& Report)
	{
		UMaterial* Material = LoadObject<UMaterial>(nullptr,
			TEXT("/Game/Blueprints/Water/M_Realistic_Water.M_Realistic_Water"));
		UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(nullptr,
			TEXT("/Game/Blueprints/Water/MPC_Water_Custom.MPC_Water_Custom"));
		if (!Material || !Collection)
		{
			Report = TEXT("FAILED: water master or parameter collection missing.");
			return false;
		}
		UMaterialExpressionCustom* Mask = nullptr;
		UMaterialExpressionVectorParameter* Tint = nullptr;
		TArray<UMaterialExpression*> Expressions;
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			Expressions.Add(Expression);
			if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression))
			{
				if (Custom->Description == OverlayDescription)
				{
					Report = TEXT("SUCCESS: surface overlay graph already upgraded.");
					return true;
				}
				if (Custom->Description == TEXT("SW Vortex Preview Circular Mask")) Mask = Custom;
			}
			if (UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(Expression))
			{
				if (Vector->ParameterName == TEXT("SW Vortex Preview Tint Color")) Tint = Vector;
			}
		}
		const FCustomInput* PositionInput = Mask ? Mask->Inputs.FindByPredicate(
			[](const FCustomInput& Input) { return Input.InputName == TEXT("WorldPosition"); }) : nullptr;
		const FCustomInput* FeatherInput = Mask ? Mask->Inputs.FindByPredicate(
			[](const FCustomInput& Input) { return Input.InputName == TEXT("EdgeFeather"); }) : nullptr;
		if (!Mask || !Tint || !PositionInput || !FeatherInput
			|| !PositionInput->Input.Expression || !FeatherInput->Input.Expression)
		{
			Report = TEXT("FAILED: expected existing circular mask, position, feather and tint inputs.");
			return false;
		}
		const FExpressionInput PositionConnection = PositionInput->Input;
		const FExpressionInput FeatherConnection = FeatherInput->Input;
		TArray<FExpressionInput*> MaskConsumers;
		TArray<FExpressionInput*> TintConsumers;
		for (UMaterialExpression* Expression : Expressions)
		{
			if (Expression == Mask) continue;
			for (FExpressionInputIterator It(Expression); It; ++It)
			{
				FExpressionInput* Input = It.Input;
				if (Input && Input->Expression == Mask) MaskConsumers.Add(Input);
				if (Input && Input->Expression == Tint) TintConsumers.Add(Input);
			}
		}
		if (MaskConsumers.Num() != 2 || TintConsumers.Num() != 2
			|| Collection->VectorParameters.Num() + 2 * USWWaterSkillOverlaySubsystem::MaxOverlayCount > 1024)
		{
			Report = TEXT("FAILED: unexpected mask/tint consumers or collection capacity.");
			return false;
		}

		Material->Modify();
		Collection->Modify();
		auto AddVector = [Collection](FName Name)
		{
			if (!Collection->VectorParameters.ContainsByPredicate(
				[Name](const FCollectionVectorParameter& Parameter) { return Parameter.ParameterName == Name; }))
			{
				FCollectionVectorParameter& Parameter = Collection->VectorParameters.AddDefaulted_GetRef();
				Parameter.ParameterName = Name;
				Parameter.DefaultValue = FLinearColor::Transparent;
			}
		};
		if (!Collection->ScalarParameters.ContainsByPredicate([](const FCollectionScalarParameter& Parameter)
			{ return Parameter.ParameterName == TEXT("SW_WaterSkillCount"); }))
		{
			FCollectionScalarParameter& Count = Collection->ScalarParameters.AddDefaulted_GetRef();
			Count.ParameterName = TEXT("SW_WaterSkillCount");
			Count.DefaultValue = 0.0f;
		}
		for (int32 Index = 0; Index < USWWaterSkillOverlaySubsystem::MaxOverlayCount; ++Index)
		{
			AddVector(*FString::Printf(TEXT("SW_WaterSkillCenterRadius%d"), Index));
			AddVector(*FString::Printf(TEXT("SW_WaterSkillColor%d"), Index));
		}
		for (FCollectionScalarParameter& Parameter : Collection->ScalarParameters)
		{
			if (Parameter.ParameterName == TEXT("SW_VortexPreviewEnabled")) Parameter.DefaultValue = 0.0f;
		}
		Collection->PostEditChange();

		auto MakeCollectionInput = [Material, Collection](FName Name, int32 Y)
		{
			UMaterialExpressionCollectionParameter* Node = CastChecked<UMaterialExpressionCollectionParameter>(
				UMaterialEditingLibrary::CreateMaterialExpression(Material,
					UMaterialExpressionCollectionParameter::StaticClass(), 9500, Y));
			Node->Collection = Collection;
			Node->ParameterName = Name;
			Node->ParameterId = Collection->GetParameterId(Name);
			Node->ExpressionGUID = FGuid::NewGuid();
			return Node;
		};
		UMaterialExpressionComponentMask* Alpha = CastChecked<UMaterialExpressionComponentMask>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material,
				UMaterialExpressionComponentMask::StaticClass(), 10400, 3700));
		Alpha->R = Alpha->G = Alpha->B = false;
		Alpha->A = true;
		Alpha->Desc = TEXT("SW Water Skill Overlay Alpha");
		UMaterialExpressionComponentMask* RGB = CastChecked<UMaterialExpressionComponentMask>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material,
				UMaterialExpressionComponentMask::StaticClass(), 10400, 3500));
		RGB->R = RGB->G = RGB->B = true;
		RGB->A = false;
		RGB->Desc = TEXT("SW Water Skill Overlay RGB");
		Mask->Modify();
		Mask->Inputs.Reset();
		auto AddInput = [Mask](FName Name, const FExpressionInput& Connection)
		{
			FCustomInput& Input = Mask->Inputs.AddDefaulted_GetRef();
			Input.InputName = Name;
			Input.Input = Connection;
		};
		auto ConnectInput = [&AddInput](FName Name, UMaterialExpression* Expression)
		{
			FExpressionInput Connection;
			Connection.Connect(0, Expression);
			AddInput(Name, Connection);
		};
		AddInput(TEXT("WorldPosition"), PositionConnection);
		AddInput(TEXT("EdgeFeather"), FeatherConnection);
		ConnectInput(TEXT("PreviewTint"), Tint);
		ConnectInput(TEXT("Count"), MakeCollectionInput(TEXT("SW_WaterSkillCount"), 4600));
		FString Code = TEXT("float4 Result = float4(0, 0, 0, 0);\n[branch] if (Count < 0.5) return Result;\n");
		for (int32 Index = 0; Index < USWWaterSkillOverlaySubsystem::MaxOverlayCount; ++Index)
		{
			const FName CenterName(*FString::Printf(TEXT("CenterRadius%d"), Index));
			const FName ColorName(*FString::Printf(TEXT("Color%d"), Index));
			UMaterialExpressionCollectionParameter* Center = MakeCollectionInput(
				*FString::Printf(TEXT("SW_WaterSkillCenterRadius%d"), Index), 4800 + Index * 240);
			UMaterialExpressionCollectionParameter* Color = MakeCollectionInput(
				*FString::Printf(TEXT("SW_WaterSkillColor%d"), Index), 4920 + Index * 240);
			// Collection vector output 0 contains all four components.
			FExpressionInput CenterConnection;
			CenterConnection.Connect(0, Center);
			FExpressionInput ColorConnection;
			ColorConnection.Connect(0, Color);
			AddInput(CenterName, CenterConnection);
			AddInput(ColorName, ColorConnection);
			Code += FString::Printf(TEXT("[branch] if (Count > %d.5) { SW_ApplyWaterSkillOverlay("
				"WorldPosition, CenterRadius%d, Color%d, PreviewTint, EdgeFeather, Result); }\n"),
				Index, Index, Index);
		}
		Code += TEXT("return Result;");
		Mask->Code = Code;
		Mask->OutputType = CMOT_Float4;
		Mask->Description = OverlayDescription;
		Mask->Desc = OverlayDescription;
		Mask->IncludeFilePaths = { TEXT("/Project/SWVortexPreview.ush") };
		Mask->RebuildOutputs();
		Alpha->Input.Connect(0, Mask);
		RGB->Input.Connect(0, Mask);
		for (FExpressionInput* Input : MaskConsumers) Input->Connect(0, Alpha);
		for (FExpressionInput* Input : TintConsumers) Input->Connect(0, RGB);
		UMaterialEditingLibrary::RecompileMaterial(Material);
		GShaderCompilingManager->FinishAllCompilation();
		const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
		if (!Resource || !Resource->GetCompileErrors().IsEmpty())
		{
			Report = TEXT("FAILED: material compilation failed; assets not saved.\n");
			if (Resource)
			{
				for (int32 Index = 0; Index < FMath::Min(3, Resource->GetCompileErrors().Num()); ++Index)
				{
					Report += Resource->GetCompileErrors()[Index] + TEXT("\n");
				}
			}
			return false;
		}
		if (!SaveAsset(Collection) || !SaveAsset(Material))
		{
			Report = TEXT("FAILED: could not save both water overlay assets.");
			return false;
		}
		Report = FString::Printf(TEXT("SUCCESS: %d slots; mask consumers=%d; tint consumers=%d; shader errors=0.\n"
			"Saved /Game/Blueprints/Water/MPC_Water_Custom\nSaved /Game/Blueprints/Water/M_Realistic_Water\n"
			"Existing preview tint: R=%g G=%g B=%g\n"),
			USWWaterSkillOverlaySubsystem::MaxOverlayCount, MaskConsumers.Num(), TintConsumers.Num(),
			Tint->DefaultValue.R, Tint->DefaultValue.G, Tint->DefaultValue.B);
		return true;
	}

	FAutoConsoleCommand UpgradeCommand(
		TEXT("SW.WaterSkills.UpgradeSurfaceOverlay"),
		TEXT("Upgrade and save the water master and MPC for simultaneous ordered skill overlays."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			FString Report;
			const bool bSuccess = Upgrade(Report);
			FFileHelper::SaveStringToFile(Report, *FPaths::Combine(
				FPaths::ProjectDir(), TEXT("tmp/WaterSkillOverlayAuthoring.txt")));
			UE_LOG(LogTemp, Display, TEXT("Water skill overlay authoring: %s"), bSuccess ? TEXT("success") : TEXT("failed"));
			if (FParse::Param(FCommandLine::Get(), TEXT("SWOverlayExit")))
			{
				FPlatformMisc::RequestExit(false);
			}
		}));
}
