// Copyright (c) 2025-2026 Winyunq. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MassBattleEditorVatSource.h"
#include "MassBattleUnitEditorMCPApi.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMassBattleUnitEditorMCPApi, Log, All);

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
	 * @brief Choose styles and recipes for VAT unit authoring.
	 * @details Reads the installed unit style profiles and authoring recipes so a caller can choose an existing profile id before constructing a VAT or organization spec.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. This entry parses the options object before enumerating installed profiles.
	 * @return Available style and recipe profiles used to select profile IDs for authoring requests.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorListProfiles(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Inspect the complete JSON settings of a style or recipe.
	 * @details Resolves a style or recipe profile by its type and id, reads its JSON file and returns both the file identity and complete profile contents.
	 * @param[in] ProfileType Profile kind: style or recipe.
	 * @param[in] ProfileId Existing profile id returned by the profile-list tool.
	 * @return The selected profile's complete JSON settings and source file path; an unavailable or malformed profile produces an error.
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
	 * @brief Inspect source assets and output layout before VAT generation.
	 * @details Output paths are derived from the source mesh, unit name and target layout. Recipe defaults, generated mesh/animation/LOD references and the explicit unit_patch are merged in that order to form the proposed unit configuration.
	 * @param[in] SpecJson JSON object describing the source skeletal_mesh and unit_name, target_package_path and optional recipe_id/style_profile, animation discovery paths and unit_patch. target_unit selects an existing unit; template_unit and target_unit_package_path describe a new unit.
	 * @return The resolved output layout, discovered inputs and proposed generation steps, with applicability and warnings describing unresolved problems.
	 * @see @ref UMassBattleUnitEditorMCPApi::MCP_EditorApplyCreateVatUnit for the primary generation entry.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorPlanCreateVatUnit(const FString& SpecJson);

	/**
	 * @ProductDocument
	 * @brief Check required VAT fields and referenced assets.
	 * @details Resolves the skeletal mesh, parent material, renderer and Niagara asset references, checks the animation category mapping and VAT settings, and combines the findings with the planned execution steps.
	 * @param[in] SpecJson VAT generation specification using the fields documented by @ref UMassBattleUnitEditorMCPApi::MCP_EditorApplyCreateVatUnit. Mesh, animation, material, renderer, output and subtype settings are checked without generating assets.
	 * @return The validation conclusion in valid, field problems in issues and proposed workflow in execution_preview.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorValidateCreateVatUnit(const FString& SpecJson);

	/**
	 * @ProductDocument
	 * @brief Create a unit that plays baked skeletal animation through a static mesh.
	 * @details The skeletal mesh is converted into a static mesh with LODs and instances of the parent material. For each LOD, bone or vertex mode samples the animations into VAT textures; these textures, an animation-index texture and decoding parameters are assigned to that LOD's materials so the static mesh can reproduce the animation. The renderer receives the mesh, Niagara system and subtype; the unit receives Visualize.RendererClass, AnimShared.AnimData and the baked animation sample rate. New units are created through @ref UMassBattleUnitMCPApi::MCP_UnitCreate, then configuration is written through @ref UMassBattleUnitMCPApi::MCP_UnitPlanMergeUpdate and @ref UMassBattleUnitMCPApi::MCP_UnitApplyPlan.
	 * @param[in] SpecJson JSON specification containing skeletal_mesh, unit_name, target_package_path, parent_material, source_renderer_class, niagara_system, vat_sample_rate and a nonempty animations category-to-AnimSequence mapping. target_unit selects an existing unit; otherwise template_unit, target_unit_package_path and subtype describe creation. dry_run/preview_only selects a preview; overwrite_existing controls replacement of generated outputs. compact_response selects result detail: false in this native entry, true by default in the MCP wrapper, with an explicit JSON value taking precedence.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return Generation-step results or a compact summary; previews return the plan and validation. Validation failures are described by validation.valid and validation.issues; execution_steps records completed and failed steps when execution stops.
	 * @note Validation is performed automatically; diagnostic plan/validate calls are optional before this primary entry.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorApplyCreateVatUnit(const FString& SpecJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Inspect the VAT specification resolved from selected assets.
	 * @details The editor selection or selected_assets is resolved into a source mesh and animation set, then passed as a specification to @ref UMassBattleUnitEditorMCPApi::MCP_EditorPlanCreateVatUnit.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. selected_assets supplies an explicit selection. Additional generation options feed the selection resolver and returned resolved_spec.
	 * @return The resolved selection, VAT specification and generation plan; ready_to_apply identifies whether the resolved request is ready for execution.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorPlanCreateVatUnitFromSelection(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Resolve selected source assets into a specification and generate a VAT unit.
	 * @details Selection resolution produces the mesh, animation and output settings used by @ref UMassBattleUnitEditorMCPApi::MCP_EditorApplyCreateVatUnit. The resolved selection and specification are included with the generation result.
	 * @param[in] OptionsJson JSON options for selection resolution and VAT generation. selected_assets overrides the current selection. compact_response controls response detail; its MCP wrapper default is true and an explicit JSON value takes precedence.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return The resolved selection and specification together with apply_result or apply_summary; failures identify selection resolution or generation problems.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorApplyCreateVatUnitFromSelection(const FString& OptionsJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Check component materials and weapon bones before assembling an Actor.
	 * @details Spawns/resolves the requested Actor for inspection, evaluates included skeletal/static components, effective materials and weapon bind bones, and reports whether the assembly can be produced. Bounds are reported in root-skeleton model space without recentering. No generated assets are written.
	 * @param[in] ActorPath Actor Blueprint or class object path. When supplied, it becomes actor_class in the inspection spec.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. Optional component_overrides selects assembly participation and component-specific authoring overrides.
	 * @return An assembly inspection containing components, materials, weapon binding information, bounds and issues; can_assemble indicates assembly feasibility.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorInspectActorAssembly(const FString& ActorPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Check skin weights, frame motion, lookup UVs and animation settings.
	 * @details Loads the VAT data and referenced skeletal/static meshes, audits deforming skin weights, position-frame motion and lookup UVs, and compares baked animation paths with the selected soldier animation profile. This entry reports evidence without baking or editing assets.
	 * @param[in] SpecJson JSON containing vat_data_asset (or anim_to_texture_data_asset); skeletal_mesh and static_mesh select the meshes explicitly. ground_tolerance_uu configures the ground audit tolerance in Unreal units.
	 * @return The VAT animation audit, including skin-weight, frame-motion, animation-resource and UV evidence together with detected issues.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorInspectVatAnimation(const FString& SpecJson);

	/**
	 * @ProductDocument
	 * @brief Inspect an Actor assembly plan and its resolved VAT specification.
	 * @details Actor inspection supplies component meshes, materials and weapon bindings. Animation, sample-rate and LOD settings are resolved together with the proposed assembled mesh path to produce resolved_vat_spec.
	 * @param[in] SpecJson JSON object with actor_class, optional component_overrides and the desired output folder/name. VAT settings use the fields documented by @ref UMassBattleUnitEditorMCPApi::MCP_EditorApplyCreateVatUnit; animation, sample-rate and LOD values are resolved from the Actor and supplied settings.
	 * @return The resolved_vat_spec, proposed assembled mesh and resolved animation, sample-rate and LOD settings; applicable and issues describe executability.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorPlanCreateVatUnitFromActor(const FString& SpecJson);

	/**
	 * @ProductDocument
	 * @brief Combine an Actor's components and weapon into an animated VAT unit.
	 * @details The resolved Actor components and weapon are assembled into an animation-compatible skeletal mesh. That mesh and the resolved VAT specification are then passed to @ref UMassBattleUnitEditorMCPApi::MCP_EditorApplyCreateVatUnit to build the unit resources.
	 * @param[in] SpecJson JSON object with actor_class, optional component_overrides and VAT generation settings. assembled_skeletal_mesh_path selects the assembly output; otherwise target_package_path and unit_name determine it, with assembled_skeletal_mesh_name overriding the name. overwrite_existing selects replacement and dry_run/preview_only selects preview. compact_response controls response detail; its MCP wrapper default is true and an explicit JSON value takes precedence.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return The plan for a preview request, or the assembled skeletal mesh, resolved VAT specification and vat_result after execution; failures report the relevant assembly or VAT step.
	 * @see @ref UMassBattleUnitEditorMCPApi::MCP_EditorPlanCreateVatUnitFromActor for the resolved inputs and assembly preview.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorApplyCreateVatUnitFromActor(const FString& SpecJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Inspect destination folders for a unit and its linked assets.
	 * @details Resolves the style layout and discovers the unit and linked generated/source assets selected by the organization policy. It calculates source/destination moves and collision status without moving or saving assets.
	 * @param[in] UnitPath Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. style_profile chooses defaults; target_root or target_package_path selects the destination. unit_name, use_family_folder, generated_folder_name/target_folder_name and managed_roots customize the existing layout policy.
	 * @return A move list giving source paths, destination paths and checks for each asset, together with the resolved style layout.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorPlanOrganizeUnitAssets(const FString& UnitPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Move a unit and its linked source and generated assets into a style layout.
	 * @details Rebuilds the same organization plan and returns it by default because dry_run=true. With dry_run=false it executes the eligible AssetTools moves and reports each performed move and actual failure.
	 * @param[in] UnitPath Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. Uses the same layout options as the organization planner. dry_run defaults to true; preview_only is also accepted.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return The move plan for a preview, or per-step results and the actual moved count after execution; failure reports retain the performed move results.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorApplyOrganizeUnitAssets(const FString& UnitPath, const FString& OptionsJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Discover VAT authoring and asset organization capabilities in the editor.
	 * @details Returns categorized capabilities for the non-UI counterpart of the MassBattleTools editor widget. The list describes available profile, VAT, Actor assembly and organization workflows without executing them.
	 * @return The editor API identity, capability description and tools grouped by authoring workflow.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|UnitEditor")
	static FString MCP_EditorGetStatus();
};
