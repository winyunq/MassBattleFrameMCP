// Copyright (c) 2025-2026 Winyunq. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MassBattleEditorVatSource.h"
#include "MassBattleUnitEditorMCPApi.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMassBattleUnitEditorMCPApi, Log, All);

class UMassBattleATTDataAsset;

/**
 * MCP version of the MassBattle editor workflow.
 *
 * The original MassBattleEditor module exposes Blueprint-callable editor tools
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
	 * @brief List MassBattle unit style profiles and authoring recipes.
	 * @details MCP tool: `editor_list_profiles`. Reads the installed unit style profiles and authoring recipes so a caller can choose an existing profile id before constructing a VAT or organization spec.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. This entry parses the options object before enumerating installed profiles.
	 * @return JSON with success and profiles. Malformed options returns success=false and error.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorListProfiles(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Read one MassBattle unit style profile or recipe.
	 * @details MCP tool: `editor_get_profile`. Resolves a style or recipe profile by its type and id, reads its JSON file and returns both the file identity and complete profile contents.
	 * @param[in] ProfileType Profile kind: style or recipe.
	 * @param[in] ProfileId Existing profile id returned by the profile-list tool.
	 * @return JSON with success, file and profile. A missing profile or invalid profile JSON returns success=false and error.
	 * @see @ref UMassBattleUnitEditorMCPApi::MCP_EditorListProfiles to discover ids.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorGetProfile(const FString& ProfileType, const FString& ProfileId);

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

	/** Plan adding or replacing a unit animation set using MassBattleEditor asset-discovery/conversion functions. */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorPlanAddAnimationsToUnit(const FString& UnitPath, const FString& SpecJson);

	/** Validate whether an animation-set edit can produce an applicable unit merge plan. */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorValidateAddAnimationsToUnit(const FString& UnitPath, const FString& SpecJson);

	/** Convenience wrapper: plan animation-set edit and apply it when applicable. */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorApplyAddAnimationsToUnit(const FString& UnitPath, const FString& SpecJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Diagnostic: preview the MassBattleTools DoAll-equivalent VAT unit plan; strict apply still requires complete canonical inputs.
	 * @details MCP tool: `editor_plan_create_vat_unit`. Resolves source mesh, recipe/style settings, animation and texture discovery, and generated-asset layout without writing generated assets. This is a diagnostic preview; its discovery aliases or inferred values do not replace the complete canonical fields required by strict apply.
	 * @param[in] SpecJson JSON describing the source skeletal_mesh and unit_name, discovery paths, optional recipe_id/style_profile and intended generated asset layout. Use canonical fields skeletal_mesh, unit_name, target_package_path, parent_material, source_renderer_class, niagara_system, vat_sample_rate and a nonempty animations category-to-AnimSequence mapping. Supply target_unit to update a unit, or template_unit, target_unit_package_path and subtype to create one. Strict execution rejects legacy aliases and missing-asset/static fallback switches.
	 * @return The planning JSON with resolved_layout, discovery, applicability, warnings and proposed steps, or success=false and error for invalid input/profile resolution.
	 * @see @ref UMassBattleUnitEditorMCPApi::MCP_EditorApplyCreateVatUnit for the primary generation entry.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorPlanCreateVatUnit(const FString& SpecJson);

	/**
	 * @ProductDocument
	 * @brief Diagnostic: validate DoAll-equivalent VAT unit inputs without writing assets.
	 * @details MCP tool: `editor_validate_create_vat_unit`. Checks the strict canonical VAT contract, resolves referenced assets and generates a diagnostic execution preview. It reports field-level issues and does not create, bake or save assets.
	 * @param[in] SpecJson Complete canonical VAT generation JSON. Use canonical fields skeletal_mesh, unit_name, target_package_path, parent_material, source_renderer_class, niagara_system, vat_sample_rate and a nonempty animations category-to-AnimSequence mapping. Supply target_unit to update a unit, or template_unit, target_unit_package_path and subtype to create one. Strict execution rejects legacy aliases and missing-asset/static fallback switches.
	 * @return JSON with success, validation_type=create_vat_unit, valid, issues and execution_preview. Check valid before interpreting the workflow as executable.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorValidateCreateVatUnit(const FString& SpecJson);

	/**
	 * @ProductDocument
	 * @brief Primary non-selection DoAll-equivalent VAT unit authoring entry; requires canonical complete inputs and validates before writing assets.
	 * @details MCP tool: `editor_apply_create_vat_unit`. Validates complete canonical inputs before running the MassBattleTools DoAll-equivalent skeletal-mesh VAT workflow. It creates or updates the required VAT data, materials, renderer and unit through MassBattleEditor. dry_run/preview_only requests validation and execution preview instead of asset writes. The MCP tool defaults compact_response to true; an explicit value in spec overrides that default.
	 * @param[in] SpecJson Complete canonical VAT generation JSON. Use canonical fields skeletal_mesh, unit_name, target_package_path, parent_material, source_renderer_class, niagara_system, vat_sample_rate and a nonempty animations category-to-AnimSequence mapping. Supply target_unit to update a unit, or template_unit, target_unit_package_path and subtype to create one. Strict execution rejects legacy aliases and missing-asset/static fallback switches. Optional dry_run/preview_only controls preview, overwrite_existing controls existing generated outputs, and compact_response selects compact results.
	 * @param[in] bSaveAssets Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
	 * @return JSON with success, dry_run, save_assets and execution_steps or a compact apply summary. Validation failures include valid=false, issues and execution_preview; step failures return their actual result.
	 * @note Validation is performed automatically; diagnostic plan/validate calls are optional before this primary entry.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorApplyCreateVatUnit(const FString& SpecJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Diagnostic: infer the DoAll spec from current editor selection or selected_assets and return it for review.
	 * @details MCP tool: `editor_plan_create_vat_unit_from_selection`. Resolves current editor-selected assets, or the explicit selected_assets override, into a VAT creation spec and returns the resulting diagnostic plan. It exposes the resolved inputs for review without generating assets.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. selected_assets supplies an explicit selection. Additional generation options feed the selection resolver and returned resolved_spec.
	 * @return JSON with success, selection, resolved_spec, create_plan and ready_to_apply. Empty/unresolvable selections return error information.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorPlanCreateVatUnitFromSelection(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Primary one-click current selection -> generate entry matching the MassBattleTools DoAll workflow.
	 * @details MCP tool: `editor_apply_create_vat_unit_from_selection`. Builds the canonical VAT spec from the current selection or selected_assets and executes the same primary DoAll generation workflow. The MCP tool defaults compact_response to true; explicit options override that value. The selection and resolved inputs remain visible in the response.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. selected_assets overrides editor selection; generation options and compact_response are passed to the resolved VAT workflow.
	 * @param[in] bSaveAssets Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
	 * @return JSON with success, selection, resolved_spec and apply_result or apply_summary; failed selection resolution or generation returns the corresponding error.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorApplyCreateVatUnitFromSelection(const FString& OptionsJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Inspect an Actor's modular skeletal/static meshes, effective materials, and resolved weapon bind bones without writing assets.
	 * @details MCP tool: `editor_inspect_actor_assembly`. Spawns/resolves the requested Actor for inspection, evaluates included skeletal/static components, effective materials and weapon bind bones, and reports whether the assembly can be produced. Bounds are reported in root-skeleton model space without recentering. No generated assets are written.
	 * @param[in] ActorPath Actor Blueprint or class object path. When supplied, it becomes actor_class in the inspection spec.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. Optional component_overrides selects assembly participation and component-specific authoring overrides.
	 * @return JSON with success, inspection_type, actor_class, root_component, component counts, components, body_bounds, can_assemble and issues.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorInspectActorAssembly(const FString& ActorPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Audit assembled skin weights, VAT position-frame motion, lookup UVs, and the baked soldier animation profile.
	 * @details MCP tool: `editor_inspect_vat_animation`. Loads the VAT data and referenced skeletal/static meshes, audits deforming skin weights, position-frame motion and lookup UVs, and compares baked animation paths with the selected soldier animation profile. This entry reports evidence without baking or editing assets.
	 * @param[in] SpecJson JSON containing vat_data_asset (or anim_to_texture_data_asset); skeletal_mesh and static_mesh select the meshes explicitly. ground_tolerance_uu configures the ground audit tolerance in Unreal units.
	 * @return The VAT animation audit JSON, including validity, deforming-weight and motion findings, animation evidence and mesh/UV issues. Invalid JSON or unresolved inputs return success=false and error.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorInspectVatAnimation(const FString& SpecJson);

	/**
	 * @ProductDocument
	 * @brief Plan Actor component assembly followed by strict VAT unit authoring without writing assets.
	 * @details MCP tool: `editor_plan_create_vat_unit_from_actor`. Inspects the Actor assembly, resolves animation/sample-rate/LOD defaults, validates the required VAT fields and assembly output, then returns a canonical resolved_vat_spec. It reports collisions and assembly issues without creating generated assets.
	 * @param[in] SpecJson JSON with actor_class and optional component_overrides, plus the canonical VAT unit fields. assembled_skeletal_mesh_path chooses the assembly asset explicitly; otherwise target_package_path and unit_name produce SKM_<unit_name>_Assembled, overridable by assembled_skeletal_mesh_name. overwrite_existing controls replacing an assembly output. Required VAT settings include unit_name, target_package_path, parent_material, source_renderer_class, niagara_system, positive vat_sample_rate, animations, and either target_unit or template_unit/target_unit_package_path/subtype.
	 * @return JSON with applicable, assembled_skeletal_mesh, assembly_status, inspection, animation_resolution, vat_sample_rate_resolution, vat_lod_resolution, resolved_vat_spec and issues.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorPlanCreateVatUnitFromActor(const FString& SpecJson);

	/**
	 * @ProductDocument
	 * @brief Assemble an Actor and configured weapon into an animation-compatible SkeletalMesh, then author its strict VAT unit.
	 * @details MCP tool: `editor_apply_create_vat_unit_from_actor`. Builds and validates the Actor assembly plan, creates the animation-compatible assembled SkeletalMesh, and passes the resolved canonical spec to the primary VAT generation entry. dry_run/preview_only returns the plan without creating outputs. The MCP tool defaults compact_response to true, overridable in spec.
	 * @param[in] SpecJson JSON with actor_class and optional component_overrides, plus the canonical VAT unit fields. assembled_skeletal_mesh_path chooses the assembly asset explicitly; otherwise target_package_path and unit_name produce SKM_<unit_name>_Assembled, overridable by assembled_skeletal_mesh_name. overwrite_existing controls replacing an assembly output. Includes the canonical VAT inputs documented by the Actor planning entry, plus dry_run/preview_only and compact_response.
	 * @param[in] bSaveAssets Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
	 * @return JSON with the plan or assembly result, skeletal_mesh, resolved_vat_spec and vat_result. Inapplicable plans or failed assembly/VAT steps return their actual errors.
	 * @see @ref UMassBattleUnitEditorMCPApi::MCP_EditorPlanCreateVatUnitFromActor for the resolved inputs and assembly preview.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorApplyCreateVatUnitFromActor(const FString& SpecJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Plan moving a MassBattle unit and linked generated/source assets into the selected style layout.
	 * @details MCP tool: `editor_plan_organize_unit_assets`. Resolves the style layout and discovers the unit and linked generated/source assets selected by the organization policy. It calculates source/destination moves and collision status without moving or saving assets.
	 * @param[in] UnitPath Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. style_profile chooses defaults; target_root or target_package_path selects the destination. unit_name, use_family_folder, generated_folder_name/target_folder_name and managed_roots customize the existing layout policy.
	 * @return A reviewable organization plan with moves, source/destination paths, resolved layout and per-move status, or success=false and error.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorPlanOrganizeUnitAssets(const FString& UnitPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Apply a reviewed MassBattle unit organization plan; options default to dry_run=true.
	 * @details MCP tool: `editor_apply_organize_unit_assets`. Rebuilds the same organization plan and returns it by default because dry_run=true. With dry_run=false it executes the eligible AssetTools moves and reports each performed move and actual failure.
	 * @param[in] UnitPath Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. Uses the same layout options as the organization planner. dry_run defaults to true; preview_only is also accepted.
	 * @param[in] bSaveAssets Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
	 * @return JSON with dry_run, plan, execution_steps, moved_count and moved; execution failures include success=false/error and the actual move report.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorApplyOrganizeUnitAssets(const FString& UnitPath, const FString& OptionsJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief List MassBattle unit editor workflow capabilities.
	 * @details MCP tool: `editor_get_status`. Returns categorized capabilities for the non-UI counterpart of the MassBattleTools editor widget. The list describes available profile, VAT, Actor assembly and organization workflows without executing them.
	 *
	 * @return JSON with success, api_name, description and categorized tools.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorGetStatus();
};
