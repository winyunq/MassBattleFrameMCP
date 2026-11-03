// Copyright (c) 2025-2026 Winyunq. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MassBattleUnitMCPApi.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMassBattleUnitMCPApi, Log, All);

/**
 * @brief Query, create and patch unit configuration assets and Unit Source Actors.
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
	 * @brief Find unit assets and their balance values by path.
	 * @details Scans unit config assets, filters their object paths by query, and returns compact balance summaries. load_fields controls whether assets are loaded for authored values. The returned total_scanned counts candidates before the query and result limit.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. query is case-insensitive path text; limit is clamped to 1..2000; load_fields defaults to true.
	 * @return Unit summaries and search metadata; count is the returned item count, while total_scanned counts candidates before filtering and limiting.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitList(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Inspect authored unit data and source Actor export information.
	 * @details Loads the requested unit or Unit Source Actor and returns source-aligned authored Data. The default simple view omits runtime, system and deprecated fields; a source Actor response also identifies export_path, exported_unit and last_report.
	 * @param[in] UnitPath Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. Select the requested detail/object view and field inclusion policy; the response reports detail and default_ignore_policy.
	 * @return The unit's identity and authored configuration in Data, with detail identifying the selected field view.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitGet(const FString& UnitPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Identify field names, types and nested structures accepted by unit patches.
	 * @details Builds the editable property schema of UMassBattleAgentConfigDataAsset and applies the same default field-ignore policy as unit reads. Recursive mode expands nested struct fields so a caller can build a patch from real property names.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. recursive defaults to true; depth defaults to 3 in recursive mode and 0 otherwise, and is clamped to 0..8.
	 * @return The supported unit class and editable field definitions, including their types, nested structure and default field filtering policy.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitGetSchema(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Produce JSON or CSV balance data for comparison and tuning.
	 * @details Reads the selected unit balance fields and writes a JSON or CSV table. With no output_path, files are placed under the project Saved/MassBattleEditorMCP/Exports directory. The response identifies the created file and exported unit count.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. format chooses JSON or CSV; output_path selects the destination file. Unit scan and field options choose table contents.
	 * @return The exported file's output_path, format and unit_count; failures identify invalid options or a file that could not be saved.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitExport(const FString& OptionsJson);

	/** Internal helper for update diff/apply implementation; not part of the public MCP surface. */
	static FString MCP_UnitPlanUpdate(const FString& UnitPath, const FString& PatchJson);

	/**
	 * @ProductDocument
	 * @brief Review field changes before writing a partial unit update.
	 * @details Builds and stores a union-merge plan from a partial source-aligned unit object. Omitted unit fields are retained. The asset is not modified; applicable and merge_errors describe whether the planned field changes can be applied.
	 * @param[in] UnitPath Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
	 * @param[in] UnitDataJson Partial unit JSON using source property names. Include the fields to change; a Unit Source Actor patch may also contain export_path.
	 * @return A stored update plan: plan_id identifies it for later review or application, diff describes field changes, and applicable with merge_errors explains whether it can run.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitPlanMergeUpdate(const FString& UnitPath, const FString& UnitDataJson);

	/**
	 * @ProductDocument
	 * @brief Change selected unit fields while retaining unspecified data.
	 * @details Plans a union-merge, checks success and applicable, then applies the returned plan automatically. It retains omitted fields and returns the planning result when validation blocks application.
	 * @param[in] UnitPath Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
	 * @param[in] UnitDataJson Partial source-aligned authored unit data. Obtain exact editable field names from the unit schema.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return The applied field changes and persistence result, or the planning result when the proposed update cannot be applied.
	 * @see @ref UMassBattleUnitMCPApi::MCP_UnitGetSchema for editable fields.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitMergeUpdate(const FString& UnitPath, const FString& UnitDataJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Use a template to create unit data or a Unit Source Actor.
	 * @details Creates a unit from the default or explicit template and optionally merges initial authored data. asset_type=source_actor creates a Unit Source Actor Blueprint; the ordinary branch creates the unit config. Target names must resolve to an unoccupied Unreal asset path.
	 * @param[in] CreateSpecJson JSON with either unit_path/new_unit_path or both asset_name and package_path. template_unit selects a template; unit_data supplies initial authored values. asset_type=source_actor enables the source Blueprint branch and export_path configures its output.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return The created asset's identity and persistence result; failures explain destination conflicts or initial-data merge problems.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitCreate(const FString& CreateSpecJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Inspect a stored plan's full diff and applicability.
	 * @details Loads a previously stored mutation plan and returns its complete diff and applicability without applying it. This is the review step between planning and an explicit apply.
	 * @param[in] PlanId plan_id returned by a unit planning operation.
	 * @return The complete stored plan and its diff, with applicable indicating whether it is executable; an unavailable plan produces an error.
	 * @pre Obtain PlanId from a planning operation such as @ref UMassBattleUnitMCPApi::MCP_UnitPlanMergeUpdate.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitPreviewDiff(const FString& PlanId);

	/**
	 * @ProductDocument
	 * @brief Write the changes recorded in a reviewed unit plan.
	 * @details Loads the stored plan, rejects a plan marked inapplicable, and executes its supported mutation type. The result records applied changes and the requested persistence behavior.
	 * @param[in] PlanId Identifier of the reviewed stored unit mutation plan.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return The executed plan's ID, applied_diff and saved state; rejected plans, failed mutations and save errors report their failure.
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
	 * @brief Preview or execute a unit asset deletion plan.
	 * @details Builds the explicit unit deletion plan. dry_run=true returns would_delete and the plan without changing the asset; dry_run=false applies an applicable plan through the same mutation API.
	 * @param[in] UnitPath Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. dry_run defaults to true. Deletion mode and plan validation are handled by the explicit unit deletion plan.
	 * @return A deletion preview with would_delete, or the actual deletion result; dry_run distinguishes the two and applicable explains whether the plan can execute.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitDelete(const FString& UnitPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Locate meshes, renderers and effects for unit authoring.
	 * @details Searches meshes, renderers, Niagara systems, AnimToTexture assets and existing unit configs, then sorts matches by object path for stable authoring discovery.
	 * @param[in] QueryJson JSON query object containing query text and limit. Results identify asset name, path, class, package_path and inferred style_family.
	 * @return Matching asset names, object paths, classes, package folders and inferred style families, with the returned count.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitFindAssets(const FString& QueryJson);

	/**
	 * @ProductDocument
	 * @brief Discover the unit operations available in the connected editor.
	 * @details Returns the native unit API identity and categorized tools with their descriptions. This is capability discovery and does not load or modify a unit asset.
	 * @return The unit API's identity, implementation module, dependency notes and tools grouped by capability.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitGetApiStatus();
};

/**
 * @brief Report unit style distributions and plan asset folder organization.
 */
UCLASS()
class MASSBATTLEEDITORMCP_API UMassBattleStyleMCPApi : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * @ProductDocument
	 * @brief Count units by style family, style index and path category.
	 * @details Scans the selected unit assets and aggregates their authored style indices, inferred style families and path categories. It reports organization counts without moving assets.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. Use the unit scan options to choose the assets included in the summary.
	 * @return The unit count and distributions across style families, path categories and style indices.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Style")
	static FString MCP_StyleSummarizeUnits(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Calculate destination folders and a move list from unit styles.
	 * @details Calculates destination folders from each unit style family and style index. It produces a reviewable move list only; this entry does not execute AssetTools moves.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. target_root selects the organization root; unit scan options select the source assets.
	 * @return A move proposal containing source and destination paths under target_root; dry_run_only=true identifies it as a plan rather than executed moves.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Style")
	static FString MCP_StylePlanOrganizeUnits(const FString& OptionsJson);
};
