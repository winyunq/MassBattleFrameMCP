// Copyright (c) 2025-2026 Winyunq. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MassBattleEditorVatSource.h"
#include "MassBattleUnitEditorMCPApi.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMassBattleUnitEditorMCPApi, Log, All);

class FJsonObject;
class FJsonValue;
class UMassBattleATTDataAsset;

/**
 * @brief Build VAT units from skeletal meshes or Actors and organize the resulting assets.
 * @details The original MassBattleEditor module exposes Blueprint-callable editor tools
 * for converting purchased assets into MassBattle-ready data. This API composes
 * those tools into reviewable MCP editor operations.
 */
UCLASS()
class MASSBATTLEEDITORMCP_API UMassBattleUnitEditorMCPApi : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Bake through AnimToTexture and refresh the baked static mesh's UV channel data. */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|VAT")
	static bool BakeVatTextures(UMassBattleATTDataAsset* DataAsset, float SampleRate);

	/** Resolve source-based defaults without generating assets; explicit non-empty values win. */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|VAT Source")
	static bool ResolveVatSourceDefaults(
		const FMassBattleMCPVatSource& Source,
		const FString& SearchPath, const FString& PackagePath, const FString& UnitName,
		USkeletalMesh*& OutPreviewSkeletalMesh,
		FString& OutSearchPath, FString& OutPackagePath, FString& OutUnitName,
		FString& OutErrorMessage);

	/** Prepare a mesh or assemble an Actor through the existing MCP assembly workflow. */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|VAT Source")
	static bool PrepareVatSourceMesh(
		const FMassBattleMCPVatSource& Source,
		UPARAM(ref) FString& SearchPath,
		UPARAM(ref) FString& PackagePath,
		UPARAM(ref) FString& UnitName,
		USkeletalMesh*& OutSkeletalMesh,
		bool& bOutUsedActor,
		FString& OutSearchPath,
		FString& OutErrorMessage);

	/**
	 * @ProductDocument
	 * @brief List the available unit styles.
	 * @details Lists the styles that provide creation defaults. New unit creation uses the default style when the caller omits style_profile.
	 * @param[in] OptionsJson JSON object. Pass {} to list the installed styles.
	 * @return Installed styles and their identifiers.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorListStyles(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Read a unit style and its creation defaults.
	 * @details Loads the style file identified by StyleId.
	 * @param[in] StyleId Style identifier returned by the style-list tool.
	 * @return The style contents and source file path, or an error for an unavailable or malformed style.
	 * @see @ref UMassBattleUnitEditorMCPApi::MCP_EditorListStyles to discover style identifiers.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorGetStyle(const FString& StyleId);

	/** Plan a staged unit authoring workflow across prepare, animation update, VAT create/refresh, and organization steps. */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorPlanUnitAuthoringWorkflow(const FString& SpecJson);

	/** Apply a reviewed unit authoring workflow. SpecJson defaults to dry_run=true. */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorApplyUnitAuthoringWorkflow(const FString& SpecJson, bool bSaveAssets);

	/** Plan discovery, official-style naming, and optional source-folder organization for a purchased skeletal asset pack. */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorPlanPreparePurchasedAsset(const FString& SpecJson);

	/** Apply a reviewed purchased-asset preparation plan. SpecJson defaults to dry_run=true. */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorApplyPreparePurchasedAsset(const FString& SpecJson, bool bSaveAssets);

	/** Discover compatible animation sequences across explicit and style-configured search roots. */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorDiscoverCompatibleAnimations(const FString& SkeletalMeshPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Inspect source assets and output layout before VAT generation.
	 * @details For a new unit, resolve omitted generation options from the selected style, then plan the generated asset references and explicit unit patch. An explicit target_unit selects an existing unit for a requested VAT rebuild. The unit patch contains the requested edits and generated references for that rebuild.
	 * @param[in] OptionsJson JSON object with source skeletal_mesh, unit_name, output location, optional style_profile for creation, and unit_patch. Use target_unit for an explicit rebuild of an existing unit.
	 * @return The resolved output layout, discovered inputs and proposed generation steps, with applicability and warnings describing unresolved problems.
	 * @see @ref UMassBattleUnitEditorMCPApi::MCP_EditorApplyCreateVatUnit for the primary generation entry.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorPlanCreateVatUnit(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Check required VAT fields and referenced assets.
	 * @details Resolves the skeletal mesh, parent material, renderer and Niagara asset references, checks the animation category mapping and VAT settings, and combines the findings with the planned execution steps.
	 * @param[in] OptionsJson JSON options accepted by @ref UMassBattleUnitEditorMCPApi::MCP_EditorApplyCreateVatUnit. The validation checks mesh, animation, material, renderer, output and subtype settings before generating assets.
	 * @return The validation conclusion in valid, field problems in issues and proposed workflow in execution_preview.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorValidateCreateVatUnit(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Create a unit that plays baked skeletal animation through a static mesh.
	 * @details Converts the skeletal mesh, bakes animation textures and configures the renderer. New units use the selected style for omitted creation values. A rebuild of an existing unit writes the generated references and explicitly requested unit changes.
	 * @param[in] OptionsJson JSON options for skeletal_mesh, unit_name, output, parent_material, source_renderer_class, niagara_system, vat_sample_rate and animations. Supply target_unit to rebuild an existing unit. style_profile is optional when creating a unit. dry_run or preview_only selects a preview, overwrite_existing allows replacement of generated outputs, and compact_response selects result detail.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return Generation-step results or a compact summary; previews return the plan and validation. Validation failures are described by validation.valid and validation.issues; execution_steps records completed and failed steps when execution stops.
	 * @note Validation is performed automatically; diagnostic plan/validate calls are optional before this primary entry.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorApplyCreateVatUnit(const FString& OptionsJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Inspect VAT creation options from selected assets.
	 * @details Review the selected assets and generation plan, then pass resolved_options to @ref UMassBattleUnitEditorMCPApi::MCP_EditorApplyCreateVatUnit.
	 * @param[in] OptionsJson JSON object. selected_assets supplies asset paths for selection resolution. Additional generation options are included in resolved_options.
	 * @return The resolved selection, generation options and plan. ready_to_apply indicates whether the request is ready for execution.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorPlanCreateVatUnitFromSelection(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Check component materials and weapon bones before assembling an Actor.
	 * @details Spawns/resolves the requested Actor for inspection, evaluates included skeletal/static components, effective materials and weapon bind bones, and reports whether the assembly can be produced. Bounds are reported in root-skeleton model space without recentering. No generated assets are written.
	 * @param[in] ActorPath Actor Blueprint or class object path. When supplied, it sets actor_class for the inspection.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. Optional component_overrides selects assembly participation and component-specific authoring overrides.
	 * @return An assembly inspection containing components, materials, weapon binding information, bounds and issues; can_assemble indicates assembly feasibility.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorInspectActorAssembly(const FString& ActorPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Check skin weights, frame motion, lookup UVs and animation settings.
	 * @details Loads the VAT data and referenced meshes, audits deforming skin weights, position-frame motion and lookup UVs, and reports the baked animations without editing assets.
	 * @param[in] OptionsJson JSON containing vat_data_asset or anim_to_texture_data_asset. skeletal_mesh and static_mesh select the meshes explicitly. ground_tolerance_uu sets the ground audit tolerance in Unreal units.
	 * @return The VAT animation audit, including skin-weight, frame-motion, animation-resource and UV evidence together with detected issues.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorInspectVatAnimation(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Inspect the Actor assembly plan and VAT creation options.
	 * @details Inspects the Actor's meshes, materials and bindings. For a new unit the style fills omitted generation options. A rebuild uses the supplied options and an explicit target.
	 * @param[in] OptionsJson JSON object with actor_class, optional component_overrides and the options documented by @ref UMassBattleUnitEditorMCPApi::MCP_EditorApplyCreateVatUnit.
	 * @return The resolved_options and proposed assembled mesh. applicable and issues report whether the plan can run.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorPlanCreateVatUnitFromActor(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Combine an Actor's components and weapon into an animated VAT unit.
	 * @details Assembles the Actor components into an animation-compatible skeletal mesh, then passes that mesh and the resolved options to @ref UMassBattleUnitEditorMCPApi::MCP_EditorApplyCreateVatUnit.
	 * @param[in] OptionsJson JSON object with actor_class, optional component_overrides and VAT generation options. assembled_skeletal_mesh_path or assembled_skeletal_mesh_name selects the assembly output. overwrite_existing selects replacement and dry_run or preview_only selects preview. compact_response controls result detail.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return The plan for a preview request, or the assembled skeletal mesh, resolved_options and vat_result. Failures report the relevant assembly or VAT step.
	 * @see @ref UMassBattleUnitEditorMCPApi::MCP_EditorPlanCreateVatUnitFromActor for the resolved inputs and assembly preview.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorApplyCreateVatUnitFromActor(const FString& OptionsJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Preview or move a unit and its linked source and generated assets into a style layout.
	 * @details With dry_run=true it returns the proposed moves and collision checks in plan. With dry_run=false it executes the eligible AssetTools moves and reports each performed move and actual failure.
	 * @param[in] UnitPath Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
	 * @param[in] OptionsJson JSON object. dry_run defaults to true, and preview_only takes precedence when supplied. style_profile chooses defaults, and target_root or target_package_path selects the destination. unit_name, use_family_folder, generated_folder_name/target_folder_name and managed_roots customize the layout.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return With dry_run=true, plan contains the proposed moves and the resolved style layout. After execution, the response includes per-step results and the actual moved count.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorApplyOrganizeUnitAssets(const FString& UnitPath, const FString& OptionsJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Discover VAT authoring and asset organization capabilities in the editor.
	 * @details Lists available styles, VAT generation, Actor assembly and asset organization capabilities.
	 * @return The editor API identity, capability description and tools grouped by authoring workflow.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorGetStatus();

private:
	/// @brief 为新单位解析 style 中的 VAT 缺省项。
	static bool ResolveVatCreationOptions(const TSharedPtr<FJsonObject>& Options, const FString& SourceMeshPath, TArray<TSharedPtr<FJsonValue>>& Issues);
	/// @brief 在写入资产前验证已解析的 VAT 参数。
	static void ValidateVatOptions(const TSharedPtr<FJsonObject>& Options, TArray<TSharedPtr<FJsonValue>>& Issues);
	/// @brief 从已烘焙的动画数据生成单位制作工作流所需的 AnimShared 增量。
	static bool BuildAnimationWritePatch(const TSharedPtr<FJsonObject>& Options, FString& OutPatchJson, FString& OutError);
	/// @brief 生成关联资产的整理预览，供工作流规划与执行入口使用。
	static FString PlanOrganizeUnitAssets(const FString& UnitPath, const FString& OptionsJson);
};
