// Copyright (c) 2025-2026 Winyunq. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MassBattleUnitMCPApi.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMassBattleUnitMCPApi, Log, All);

/**
 * @brief Read and edit MassBattle unit configurations.
 * @details This API is intentionally independent from UMGMCP. It exposes stable JSON
 * entry points that any MCP transport layer can route to.
 */
UCLASS()
class MASSBATTLEEDITORMCP_API UMassBattleUnitMCPApi : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Shared implementation for the source Actor's Update action; no new MCP command. */
	static FString UpdateSource(class AMassBattleUnitSource* Source, bool bSaveAssets);
	static FString DestroySourceUnit(class AMassBattleUnitSource* Source);

	/**
	 * @ProductDocument
	 * @brief List unit configuration assets.
	 * @details MCP tool: `unit_list`. Scans unit config assets, filters their object paths by query, and returns compact balance summaries. load_fields controls whether assets are loaded for authored values. The returned total_scanned counts candidates before the query and result limit.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. query is case-insensitive path text; limit is clamped to 1..2000; load_fields defaults to true.
	 * @return JSON with success, count, total_scanned, units, roots and default_ignore_policy. Invalid JSON returns success=false with error.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitList(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Read a unit configuration.
	 * @details MCP tool: `unit_get`. Loads the requested unit or Unit Source Actor and returns source-aligned authored Data. The default simple view omits runtime, system and deprecated fields; a source Actor response also identifies export_path, exported_unit and last_report.
	 * @param[in] UnitPath Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. Select the requested detail/object view and field inclusion policy; the response reports detail and default_ignore_policy.
	 * @return JSON with success, AssetName, ObjectPath, Class, detail and Data. Invalid JSON or an unresolved unit returns success=false and error.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitGet(const FString& UnitPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Read editable unit fields.
	 * @details MCP tool: `unit_get_schema`. Builds the editable property schema of UMassBattleAgentConfigDataAsset and applies the same default field-ignore policy as unit reads. Recursive mode expands nested struct fields so a caller can build a patch from real property names.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. recursive defaults to true; depth defaults to 3 in recursive mode and 0 otherwise, and is clamped to 0..8.
	 * @return JSON with success, asset_class, fields and default_ignore_policy. Invalid JSON returns success=false and error.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitGetSchema(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Export a unit balance table.
	 * @details MCP tool: `unit_export`. Reads the selected unit balance fields and writes a JSON or CSV table. With no output_path, files are placed under the project Saved/MassBattleEditorMCP/Exports directory. The response identifies the created file and exported unit count.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. format chooses JSON or CSV; output_path selects the destination file. Unit scan and field options choose table contents.
	 * @return JSON with success, format, output_path and unit_count. Invalid options or a file-save failure returns success=false and error.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitExport(const FString& OptionsJson);

	/** Internal helper for update diff/apply implementation; not part of the public MCP surface. */
	static FString MCP_UnitPlanUpdate(const FString& UnitPath, const FString& PatchJson);

	/**
	 * @ProductDocument
	 * @brief Preview unit configuration changes.
	 * @details MCP tool: `unit_plan_write`. Builds and stores a union-merge plan from a partial source-aligned unit object. Omitted unit fields are retained. The asset is not modified; applicable and merge_errors describe whether the planned field changes can be applied.
	 * @param[in] UnitPath Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
	 * @param[in] UnitDataJson Partial unit JSON using source property names. Include the fields to change; a Unit Source Actor patch may also contain export_path.
	 * @return JSON with plan_id, plan_path, merge_semantics, patches, diff, merge_errors and applicable. Invalid JSON, an unresolved asset or no mergeable fields returns success=false and error.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitPlanMergeUpdate(const FString& UnitPath, const FString& UnitDataJson);

	/**
	 * @ProductDocument
	 * @brief Update selected unit fields.
	 * @details MCP tool: `unit_write`. Plans a union-merge, checks success and applicable, then applies the returned plan automatically. It retains omitted fields and returns the planning result when validation blocks application.
	 * @param[in] UnitPath Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
	 * @param[in] UnitDataJson Partial source-aligned authored unit data. Obtain exact editable field names from the unit schema.
	 * @param[in] bSaveAssets Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
	 * @return The planning error or the apply response, including success, saved and applied_diff.
	 * @see @ref UMassBattleUnitMCPApi::MCP_UnitGetSchema for editable fields.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitMergeUpdate(const FString& UnitPath, const FString& UnitDataJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Create a unit from a template.
	 * @details MCP tool: `unit_create`. Creates a unit from the default or explicit template and optionally merges initial authored data. asset_type=source_actor creates a Unit Source Actor Blueprint; the ordinary branch creates the unit config. Target names must resolve to an unoccupied Unreal asset path.
	 * @param[in] CreateSpecJson JSON with either unit_path/new_unit_path or both asset_name and package_path. template_unit selects a template; unit_data supplies initial authored values. asset_type=source_actor enables the source Blueprint branch and export_path configures its output.
	 * @param[in] bSaveAssets Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
	 * @return The create/apply JSON response identifying the created asset and saved state. Invalid target paths, occupied assets or initial-data merge errors return success=false and error.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitCreate(const FString& CreateSpecJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Read a unit plan and its changes.
	 * @details MCP tool: `unit_preview_plan`. Loads a previously stored mutation plan and returns its complete diff and applicability without applying it. This is the review step between planning and an explicit apply.
	 * @param[in] PlanId plan_id returned by a unit planning operation.
	 * @return JSON with success, plan_id, type, applicable, diff/diff_from_template and the complete plan. A missing or unreadable plan returns success=false and error.
	 * @pre Obtain PlanId from a planning operation such as @ref UMassBattleUnitMCPApi::MCP_UnitPlanMergeUpdate.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitPreviewDiff(const FString& PlanId);

	/**
	 * @ProductDocument
	 * @brief Apply a unit modification plan.
	 * @details MCP tool: `unit_apply_plan`. Loads the stored plan, rejects a plan marked inapplicable, and executes its supported mutation type. The result records applied changes and the requested persistence behavior.
	 * @param[in] PlanId Identifier of the reviewed stored unit mutation plan.
	 * @param[in] bSaveAssets Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
	 * @return JSON containing the mutation result, plan_id, saved and applied_diff, or success=false and error for an invalid plan, failed mutation or save failure.
	 * @pre Inspect the diff with @ref UMassBattleUnitMCPApi::MCP_UnitPreviewDiff before submitting a reviewed plan.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitApplyPlan(const FString& PlanId, bool bSaveAssets);

	/** Move a unit asset into a dated trash folder after reporting referencers. */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitDeleteSoft(const FString& UnitPath, const FString& OptionsJson);

	/** Internal helper for delete implementation; not part of the public MCP surface. */
	static FString MCP_UnitPlanDelete(const FString& UnitPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Delete a unit configuration asset.
	 * @details MCP tool: `unit_delete`. Builds the explicit unit deletion plan. dry_run=true returns would_delete and the plan without changing the asset; dry_run=false applies an applicable plan through the same mutation API.
	 * @param[in] UnitPath Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. dry_run defaults to true. Deletion mode and plan validation are handled by the explicit unit deletion plan.
	 * @return A deletion plan with dry_run and would_delete, or the applied deletion result. Invalid JSON or an inapplicable plan reports success=false/error or applicable=false.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitDelete(const FString& UnitPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Find assets for unit authoring.
	 * @details MCP tool: `unit_find_assets`. Searches meshes, renderers, Niagara systems, AnimToTexture assets and existing unit configs, then sorts matches by object path for stable authoring discovery.
	 * @param[in] QueryJson JSON query object containing query text and limit. Results identify asset name, path, class, package_path and inferred style_family.
	 * @return JSON with success, count and assets. Invalid query JSON returns success=false and error.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitFindAssets(const FString& QueryJson);

	/**
	 * @ProductDocument
	 * @brief List available unit tools.
	 * @details MCP tool: `unit_get_api_status`. Returns the native unit API identity and categorized tools with their descriptions. This is capability discovery and does not load or modify a unit asset.
	 *
	 * @return JSON with success, api_name, module, dependency_note and categorized tool metadata.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitGetApiStatus();
};

/**
 * @brief Plan unit organization by style.
 */
UCLASS()
class MASSBATTLEEDITORMCP_API UMassBattleStyleMCPApi : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * @ProductDocument
	 * @brief Summarize units by style.
	 * @details MCP tool: `style_summarize_units`. Scans the selected unit assets and aggregates their authored style indices, inferred style families and path categories. It reports organization counts without moving assets.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. Use the unit scan options to choose the assets included in the summary.
	 * @return JSON with success, unit_count, style_families, path_categories and style_indices.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Style")
	static FString MCP_StyleSummarizeUnits(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Plan unit folders by style.
	 * @details MCP tool: `style_plan_organize_units`. Calculates destination folders from each unit style family and style index. It produces a reviewable move list only; this entry does not execute AssetTools moves.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. target_root selects the organization root; unit scan options select the source assets.
	 * @return JSON with success, plan_type, target_root, dry_run_only=true and moves containing source and destination paths.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Style")
	static FString MCP_StylePlanOrganizeUnits(const FString& OptionsJson);
};
