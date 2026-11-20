// Copyright (c) 2025-2026 Winyunq. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MassBattleProjectileMCPApi.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMassBattleProjectileMCPApi, Log, All);

/**
 * @brief Maintain projectile movement, damage, triggers and lifecycle effect settings.
 * @details This API edits projectile gameplay assets only. Linked one-shot and attached
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
	 * @brief Discover projectile editing and validation operations in the editor.
	 * @details Returns projectile DataAsset capabilities, native parameter names and write-contract metadata. It identifies the schema, CRUD and validation entries available to the connected editor.
	 * @return The projectile API version, supported asset type, write contract and available tools.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileGetApiStatus();

	/**
	 * @ProductDocument
	 * @brief Filter projectile assets by path and inspect their configuration summaries.
	 * @details Scans projectile configuration DataAssets, filters object paths by query and returns stable asset summaries. load_fields selects whether authored fields are loaded for each result.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. query filters paths; limit bounds the result count; load_fields controls summary detail. Scan roots select the content scope.
	 * @return Matching projectile summaries and searched roots; count is the returned item count and total_scanned is the number scanned.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileList(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Locate projectile configuration assets by name or path.
	 * @details Uses the projectile listing query path to find configurations by path or name. It returns the same stable summaries and scan metadata as the projectile list entry.
	 * @param[in] QueryJson JSON object; pass {} to use the defaults. Uses the projectile-list query and scope options.
	 * @return Projectile search results in the same format as projectile_list, including matching assets, counts and searched roots.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileQuery(const FString& QueryJson);

	/**
	 * @ProductDocument
	 * @brief Inspect active projectile data with optional validation and references.
	 * @details Loads one projectile DataAsset and returns source-aligned authored data. The active view selects fields relevant to the configured movement and damage modes; the full view can include inactive fields. Validation and referencing assets can be included in the same read.
	 * @param[in] ProjectilePath Unreal object path of the projectile configuration DataAsset.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. view/detail and include_inactive select the field view; include_validation and include_referencers request the corresponding reports.
	 * @return The authored configuration in data and its movement and damage modes; view identifies the field view, with validation and referencers included when requested.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileGet(const FString& ProjectilePath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Obtain field types and write rules for movement and damage modes.
	 * @details Reflects projectile field names, types, enums, edit conditions and tooltips. It returns movement/damage-mode field groups and the source-aligned write, damage-ownership and lifecycle rules needed to form a valid patch. Array writes merge by index and reject appending by default; options.append_arrays=true permits append and options.array_mode=replace requests replacement. The returned ownership rule assigns normal projectile attack damage to the projectile DataAsset with Agent Attack.TimeOfHitAction=None; the lifecycle rule identifies duplicate full explosions in OnHit and OnRemoval.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. depth selects the nested schema expansion depth.
	 * @return Field definitions and mode-specific field groups, together with array-write, damage-ownership and lifecycle rules.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileGetSchema(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Create a validated projectile config from a template or class defaults.
	 * @details Builds a transient preview from the requested template or class defaults, applies initial source-aligned data and validates it before creating the real projectile DataAsset. It rejects an invalid or occupied destination instead of replacing that asset.
	 * @param[in] CreateSpecJson JSON with asset_name (alias name), package_path (alias target_package_path) and optional template_path/template plus initial projectile data. package_path must be an Unreal package path beginning with /.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return The new asset and template paths, resulting configuration and changed field paths; saved indicates whether the asset was written to disk.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileCreate(const FString& CreateSpecJson, bool bSaveAssets = true);

	/**
	 * @ProductDocument
	 * @brief Validate a partial update before changing the projectile asset.
	 * @details Applies the partial source-aligned patch to a transient copy and validates the candidate before mutating the real projectile configuration. Omitted fields are retained; use the schema write rules when changing arrays or nested authored structs.
	 * @param[in] ProjectilePath Unreal object path of the existing projectile DataAsset.
	 * @param[in] PatchJson Nonempty partial projectile JSON using real source field names. Read the projectile schema for enum values, conditions and array write rules. options.append_arrays and options.array_mode explicitly select array append/replacement behavior.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return The actual changed field paths, resulting configuration and validation report; saved identifies persistence, and failures report field, preflight or save errors.
	 * @see @ref UMassBattleProjectileMCPApi::MCP_ProjectileGetSchema for exact field/write rules.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileWrite(const FString& ProjectilePath, const FString& PatchJson, bool bSaveAssets = true);

	/**
	 * @ProductDocument
	 * @brief Find movement, damage ownership and lifecycle effect configuration issues.
	 * @details Checks movement configuration, damage ownership, trigger/lifetime behavior, lifecycle effects and duplicate explosion risks on the requested projectile. It does not rewrite the configuration.
	 * @param[in] ProjectilePath Unreal object path of the projectile DataAsset to inspect.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. include_referencers includes assets that reference the projectile.
	 * @return The projectile validation conclusion and findings in validation, with referencing assets included when requested.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileValidate(const FString& ProjectilePath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Check references before previewing or executing asset removal.
	 * @details Builds a deletion/trash move report and checks references. dry_run=true returns the report without changing the DataAsset; an explicit executable request performs the selected supported mode and returns the actual move/delete outcome.
	 * @param[in] ProjectilePath Unreal object path of the projectile configuration to remove.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. dry_run defaults to true. mode chooses the supported deletion path; trash_root selects trash placement. Referenced/unsafe/hard operations require their corresponding allow_referenced, allow_unsafe_asset_move or allow_hard_delete option.
	 * @return The removal report with referencers, applicability and blocking reasons; execution adds the actual trash destination or deletion count.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileDelete(const FString& ProjectilePath, const FString& OptionsJson);
};
