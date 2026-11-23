// Copyright (c) 2025-2026 Winyunq. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MassBattleUnitMCPApi.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMassBattleUnitMCPApi, Log, All);

class FJsonObject;
class FJsonValue;
class UMassBattleAgentConfigDataAsset;

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
	static FString UpdateSource(class AMassBattleUnitSource* Source, bool bSaveAssets, bool* OutExportChanged = nullptr);
	static FString DestroySourceUnit(class AMassBattleUnitSource* Source);
	/// @brief 解析单位资产或源码 Actor 的实际单位配置。
	static UMassBattleAgentConfigDataAsset* ResolveUnit(const FString& UnitPath, FString& OutError);
	/// @brief 合并新建单位的 Style 缺省值和显式数据，保留显式字段。
	static bool ResolveCreationPatch(const TSharedPtr<FJsonObject>& AuthoringDefaults,
		const TSharedPtr<FJsonObject>& ExplicitPatch, TSharedPtr<FJsonObject>& OutUnitPatch,
		TArray<TSharedPtr<FJsonValue>>& OutDerivations, FString& OutError);

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
	 * @details Reads the Unit Source Actor's UnitData or the unit DataAsset selected by UnitPath. Read the source to review authored settings and the exported unit to verify the result. Source responses include export_path, exported_unit and last_report. The default simple view omits runtime, system and deprecated fields.
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

	/// @brief Read the supplied fields from flat or Data-wrapped unit JSON.
	static TSharedPtr<FJsonObject> ReadUnitWriteData(const TSharedPtr<FJsonObject>& Root, FString& OutError);
	/// @brief Validate the submitted unit fields for an editor operation before it writes other assets.
	static bool ValidateUnitWrite(const FString& UnitPath, const FString& UnitDataJson, FString& OutError);

	/**
	 * @ProductDocument
	 * @brief Write the requested fields of an existing unit.
	 * @details Target the Actor Source Blueprint for routine edits or a unit DataAsset for direct edits to that asset. Fields omitted from the patch keep their current values.
	 * @param[in] UnitPath Unreal object path of the Actor Source Blueprint or unit DataAsset.
	 * @param[in] UnitDataJson Partial unit JSON using source property names. Use options.export_source=true to regenerate the linked unit when saving the Actor Source.
	 * @param[in] bSaveAssets Save the edited asset to disk. When false, edit it only in memory.
	 * @return Success or an error explaining which write failed.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitWrite(const FString& UnitPath, const FString& UnitDataJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Create a unit DataAsset or a Unit Source Actor.
	 * @details Fills omitted creation values from the selected style, preserving explicitly supplied values. asset_type=source_actor creates a Unit Source Actor Blueprint. The destination must be unoccupied.
	 * @param[in] OptionsJson JSON with unit_path or both asset_name and package_path. style_profile is optional and defaults to default. template_unit selects a template and unit_data supplies initial authored values. asset_type=source_actor selects the source Blueprint branch and export_path configures its output.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return The created asset's identity and persistence result; failures explain destination conflicts or initial-data merge problems.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitCreate(const FString& OptionsJson, bool bSaveAssets);

	/** Move a unit asset into a dated trash folder after reporting referencers. */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitDeleteSoft(const FString& UnitPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Check or execute a unit asset deletion.
	 * @details dry_run=true checks the selected asset and reports whether deletion is allowed. dry_run=false performs the deletion if its reference checks pass.
	 * @param[in] UnitPath Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. dry_run defaults to true. mode selects a soft move to trash or permanent deletion.
	 * @return Deletion eligibility or the executed deletion result.
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
