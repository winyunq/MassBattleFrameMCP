// Copyright (c) 2025-2026 Winyunq. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MassBattleUnitMCPApi.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMassBattleUnitMCPApi, Log, All);

/**
 * Unit-focused MCP API for MassBattle agent config assets.
 *
 * This API is intentionally independent from UMGMCP. It exposes stable JSON
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
	 * List MassBattle unit DataAssets with compact balance-oriented summaries.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitList(const FString& OptionsJson);

	/**
	 * Get one unit's authored data. Default options omit runtime/system/deprecated fields.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitGet(const FString& UnitPath, const FString& OptionsJson);

	/**
	 * Return editable schema and default ignore policy for UMassBattleAgentConfigDataAsset.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitGetSchema(const FString& OptionsJson);

	/**
	 * Export a balance table to JSON or CSV under Saved/MassBattleEditorMCP/Exports by default.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitExport(const FString& OptionsJson);

	/** Internal helper for update diff/apply implementation; not part of the public MCP surface. */
	static FString MCP_UnitPlanUpdate(const FString& UnitPath, const FString& PatchJson);

	/**
	 * Plan a union-write and return its diff without mutating the unit.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitPlanMergeUpdate(const FString& UnitPath, const FString& UnitDataJson);

	/**
	 * Convenience wrapper: union-merge a partial source-aligned unit JSON object and optionally save.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitMergeUpdate(const FString& UnitPath, const FString& UnitDataJson, bool bSaveAssets);

	/**
	 * Create a new unit DataAsset from the default or specified template, then optionally apply initial unit data.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitCreate(const FString& CreateSpecJson, bool bSaveAssets);

	/**
	 * Preview a saved unit mutation plan and its complete diff.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitPreviewDiff(const FString& PlanId);

	/**
	 * Apply a reviewed unit mutation plan and optionally save the unit asset.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitApplyPlan(const FString& PlanId, bool bSaveAssets);

	/** Move a unit asset into a dated trash folder after reporting referencers. */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitDeleteSoft(const FString& UnitPath, const FString& OptionsJson);

	/** Internal helper for delete implementation; not part of the public MCP surface. */
	static FString MCP_UnitPlanDelete(const FString& UnitPath, const FString& OptionsJson);

	/**
	 * Delete or soft-delete a unit explicitly. Defaults to dry_run=true.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitDelete(const FString& UnitPath, const FString& OptionsJson);

	/**
	 * Search meshes, renderers, Niagara systems, AnimToTexture assets, and existing unit configs for unit generation.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitFindAssets(const FString& QueryJson);

	/**
	 * Return categorized tool metadata for this independent MassBattleEditorMCP unit API.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Unit")
	static FString MCP_UnitGetApiStatus();
};

/** Independent MCP helpers for organizing units by style and producing move plans. */
UCLASS()
class MASSBATTLEEDITORMCP_API UMassBattleStyleMCPApi : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Summarize unit organization by style index, path category, and inferred style family.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Style")
	static FString MCP_StyleSummarizeUnits(const FString& OptionsJson);

	/**
	 * Plan style-based folder organization without moving assets.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Style")
	static FString MCP_StylePlanOrganizeUnits(const FString& OptionsJson);
};
