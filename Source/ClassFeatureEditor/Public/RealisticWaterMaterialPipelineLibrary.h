#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RealisticWaterMaterialPipelineLibrary.generated.h"

class UMaterialExpression;
class UMaterialExpressionCustom;
class UMaterialExpressionSetMaterialAttributes;
class UMaterial;

/** Editor-only helpers used to build the isolated realistic-water test material. */
UCLASS()
class CLASSFEATUREEDITOR_API URealisticWaterMaterialPipelineLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Returns the material's expression list for editor automation in UE 5.7. */
	UFUNCTION(BlueprintCallable, Category = "ArtisticSW|Editor|Water")
	static TArray<UMaterialExpression*> GetMaterialExpressions(UMaterial* Material);

	/** Returns the expression wired to a material-expression input pin. */
	UFUNCTION(BlueprintCallable, Category = "ArtisticSW|Editor|Water")
	static UMaterialExpression* GetConnectedInputExpression(
		UMaterialExpression* Expression,
		int32 InputIndex);

	/** Returns the exact compiler output pin names used for graph connections. */
	UFUNCTION(BlueprintCallable, Category = "ArtisticSW|Editor|Water")
	static TArray<FName> GetMaterialExpressionOutputNames(UMaterialExpression* Expression);

	/**
	 * Adds Normal and Roughness pins through the engine's supported C++ API and
	 * connects them. UE 5.7's Python array setter cannot safely create more than
	 * one dynamic SetMaterialAttributes pin in a single operation.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArtisticSW|Editor|Water")
	static bool ConfigureAttributeOverride(
		UMaterialExpressionSetMaterialAttributes* SetAttributes,
		UMaterialExpression* NormalExpression,
		UMaterialExpression* RoughnessExpression);

	/**
	 * Adds the visual-water Normal, Roughness, and Specular overrides while
	 * leaving the source material's remaining attributes untouched.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArtisticSW|Editor|Water")
	static bool ConfigureVisualWaterAttributeOverride(
		UMaterialExpressionSetMaterialAttributes* SetAttributes,
		UMaterialExpression* NormalExpression,
		UMaterialExpression* RoughnessExpression,
		UMaterialExpression* SpecularExpression);

	/**
	 * Adds the appearance pins used by the isolated V4 whitecap layer. The
	 * source MaterialAttributes pin remains intact, so displacement, opacity,
	 * absorption and every gameplay-facing water path are preserved.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArtisticSW|Editor|Water")
	static bool ConfigureFoamWaterAttributeOverride(
		UMaterialExpressionSetMaterialAttributes* SetAttributes,
		UMaterialExpression* BaseColorExpression,
		UMaterialExpression* RoughnessExpression,
		UMaterialExpression* SpecularExpression,
		UMaterialExpression* EmissiveExpression);

	/** Sets matching texture parameter nodes to the sampler required by a non-sRGB grayscale texture. */
	UFUNCTION(BlueprintCallable, Category = "ArtisticSW|Editor|Water")
	static int32 ConfigureLinearGrayscaleSampler(
		UMaterial* Material,
		FName TextureParameterName);

	/** Changes matching scalar-parameter defaults in an editor-built material graph. */
	UFUNCTION(BlueprintCallable, Category = "ArtisticSW|Editor|Water")
	static int32 SetScalarParameterDefault(
		UMaterial* Material,
		FName ParameterName,
		float DefaultValue);

	/** Generates missing GUIDs so parameters created by Python remain instance-editable. */
	UFUNCTION(BlueprintCallable, Category = "ArtisticSW|Editor|Water")
	static int32 InitializeMissingParameterGuids(UMaterial* Material);

	/** Configures a float4 Custom node without relying on Python's dynamic-pin array setter. */
	UFUNCTION(BlueprintCallable, Category = "ArtisticSW|Editor|Water")
	static bool ConfigureFloat4CustomExpression(
		UMaterialExpressionCustom* CustomExpression,
		const TArray<FName>& InputNames,
		const FString& Code,
		const FString& Description);

	UFUNCTION(BlueprintCallable, Category = "ArtisticSW|Editor|Water")
	static bool ConfigureFloat2CustomExpression(
		UMaterialExpressionCustom* CustomExpression,
		const TArray<FName>& InputNames,
		const FString& Code,
		const FString& Description);

	UFUNCTION(BlueprintCallable, Category = "ArtisticSW|Editor|Water")
	static bool ConfigureFloat1CustomExpression(
		UMaterialExpressionCustom* CustomExpression,
		const TArray<FName>& InputNames,
		const FString& Code,
		const FString& Description);
};
