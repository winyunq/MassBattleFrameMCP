// Copyright (c) 2025-2026 Winyunq. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MassBattleProjectileMCPApi.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMassBattleProjectileMCPApi, Log, All);

/**
 * Source-aligned CRUD, schema, and validation primitives for
 * UMassBattleProjectileConfigDataAsset.
 *
 * This API edits projectile gameplay assets only. Linked one-shot and attached
 * visuals remain MassBattle Batch FX/Niagara work and are authored through the
 * effect APIs.
 */
UCLASS()
class MASSBATTLEEDITORMCP_API UMassBattleProjectileMCPApi : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * @ProductDocument
	 * @brief List projectile DataAsset CRUD, schema, and validation capabilities.
	 * @details MCP tool: `projectile_get_api_status`. Returns projectile DataAsset capabilities, native parameter names and write-contract metadata. It identifies the schema, CRUD and validation entries available to the connected editor.
	 *
	 * @return JSON with success, api_name, version, asset_class, write_contract and tools.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileGetApiStatus();

	/**
	 * @ProductDocument
	 * @brief List MassBattle projectile configuration DataAssets.
	 * @details MCP tool: `projectile_list`. Scans projectile configuration DataAssets, filters object paths by query and returns stable asset summaries. load_fields selects whether authored fields are loaded for each result.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. query filters paths; limit bounds the result count; load_fields controls summary detail. Scan roots select the content scope.
	 * @return JSON with success, count, total_scanned, roots and projectiles, or success=false/error for invalid options.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileList(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Query MassBattle projectile configuration DataAssets by path or name.
	 * @details MCP tool: `projectile_query`. Uses the projectile listing query path to find configurations by path or name. It returns the same stable summaries and scan metadata as the projectile list entry.
	 * @param[in] QueryJson JSON object; pass {} to use the defaults. Uses the projectile-list query and scope options.
	 * @return The projectile-list response with success, counts, roots and matching projectiles.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileQuery(const FString& QueryJson);

	/**
	 * @ProductDocument
	 * @brief Read one projectile DataAsset in active-only or full source-aligned form.
	 * @details MCP tool: `projectile_get`. Loads one projectile DataAsset and returns source-aligned authored data. The active view selects fields relevant to the configured movement and damage modes; the full view can include inactive fields. Validation and referencing assets can be included in the same read.
	 * @param[in] ProjectilePath Unreal object path of the projectile configuration DataAsset.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. view/detail and include_inactive select the field view; include_validation and include_referencers request the corresponding reports.
	 * @return JSON with success, asset_name, object_path, class, view, movement_mode, damage_mode and data, plus requested validation/referencers.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileGet(const FString& ProjectilePath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Read projectile field types, enums, conditions, tooltips, and write rules.
	 * @details MCP tool: `projectile_get_schema`. Reflects projectile field names, types, enums, edit conditions and tooltips. It returns movement/damage-mode field groups and the source-aligned write, damage-ownership and lifecycle rules needed to form a valid patch. Array writes merge by index and reject appending by default; options.append_arrays=true permits append and options.array_mode=replace requests replacement. The returned ownership rule assigns normal projectile attack damage to the projectile DataAsset with Agent Attack.TimeOfHitAction=None; the lifecycle rule identifies duplicate full explosions in OnHit and OnRemoval.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. depth selects the nested schema expansion depth.
	 * @return JSON with success, class, field_count, fields, movement_mode_fields, damage_mode_fields, array_write_rule, damage_ownership_rule and lifecycle_rule.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileGetSchema(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Create a projectile DataAsset from a template or class defaults.
	 * @details MCP tool: `projectile_create`. Builds a transient preview from the requested template or class defaults, applies initial source-aligned data and validates it before creating the real projectile DataAsset. It rejects an invalid or occupied destination instead of replacing that asset.
	 * @param[in] CreateSpecJson JSON with asset_name (alias name), package_path (alias target_package_path) and optional template_path/template plus initial projectile data. package_path must be an Unreal package path beginning with /.
	 * @param[in] bSaveAssets Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
	 * @return JSON with success, asset_path, template_path, saved, changed_paths and data. Invalid input, preflight failures or asset creation errors return success=false/error.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileCreate(const FString& CreateSpecJson, bool bSaveAssets = true);

	/**
	 * @ProductDocument
	 * @brief Union-write source-aligned projectile data after transient preflight and validation.
	 * @details MCP tool: `projectile_write`. Applies the partial source-aligned patch to a transient copy and validates the candidate before mutating the real projectile configuration. Omitted fields are retained; use the schema write rules when changing arrays or nested authored structs.
	 * @param[in] ProjectilePath Unreal object path of the existing projectile DataAsset.
	 * @param[in] PatchJson Nonempty partial projectile JSON using real source field names. Read the projectile schema for enum values, conditions and array write rules. options.append_arrays and options.array_mode explicitly select array append/replacement behavior.
	 * @param[in] bSaveAssets Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
	 * @return JSON with success, asset_path, saved, changed_paths, data and validation, or success=false/error for invalid fields, failed preflight or saving.
	 * @see @ref UMassBattleProjectileMCPApi::MCP_ProjectileGetSchema for exact field/write rules.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileWrite(const FString& ProjectilePath, const FString& PatchJson, bool bSaveAssets = true);

	/**
	 * @ProductDocument
	 * @brief Validate movement, damage ownership, triggers, lifecycle FX, and duplicate explosions.
	 * @details MCP tool: `projectile_validate`. Checks movement configuration, damage ownership, trigger/lifetime behavior, lifecycle effects and duplicate explosion risks on the requested projectile. It does not rewrite the configuration.
	 * @param[in] ProjectilePath Unreal object path of the projectile DataAsset to inspect.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. include_referencers includes assets that reference the projectile.
	 * @return JSON with success and validation, plus requested referencers. An invalid path or options object returns success=false/error.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileValidate(const FString& ProjectilePath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Plan or delete a projectile DataAsset; dry_run=true by default.
	 * @details MCP tool: `projectile_delete`. Builds a deletion/trash move report and checks references. dry_run=true returns the report without changing the DataAsset; an explicit executable request performs the selected supported mode and returns the actual move/delete outcome.
	 * @param[in] ProjectilePath Unreal object path of the projectile configuration to remove.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. dry_run defaults to true. mode chooses the supported deletion path; trash_root selects trash placement. Referenced/unsafe/hard operations require their corresponding allow_referenced, allow_unsafe_asset_move or allow_hard_delete option.
	 * @return JSON with dry_run, mode, source_path, referencers, applicable and blocked_reason; executed requests add moved/trash_path or deleted/deleted_count and any error.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileDelete(const FString& ProjectilePath, const FString& OptionsJson);
};
