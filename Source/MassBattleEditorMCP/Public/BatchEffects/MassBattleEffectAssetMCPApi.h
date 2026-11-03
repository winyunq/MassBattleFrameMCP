// Copyright (c) 2025-2026 Winyunq. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MassBattleEffectAssetMCPApi.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMassBattleEffectAssetMCPApi, Log, All);

/**
 * Low-level effect asset MCP primitives.
 *
 * These tools intentionally do not perform one-click conversion. They provide
 * asset discovery, text inspection, duplication, and MassBattle FX renderer
 * default writes so a skill can assemble a conversion workflow.
 */
UCLASS()
class MASSBATTLEEDITORMCP_API UMassBattleEffectAssetMCPApi : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * @ProductDocument
	 * @brief List generic MassBattle effect asset API capabilities.
	 * @details The effect_asset_get_api_status MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|BatchEffects")
	static FString MCP_EffectAssetGetApiStatus();

	/**
	 * @ProductDocument
	 * @brief Query Niagara, Cascade, material, Blueprint, and related visual effect assets.
	 * @details The effect_asset_query MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|BatchEffects")
	static FString MCP_EffectAssetQuery(const FString& QueryJson);

	/**
	 * @ProductDocument
	 * @brief Read a typed summary for an effect-related asset.
	 * @details The effect_asset_read_summary MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|BatchEffects")
	static FString MCP_EffectAssetReadSummary(const FString& AssetPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Export deterministic text for an effect-related asset.
	 * @details The effect_asset_export_text MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|BatchEffects")
	static FString MCP_EffectAssetExportText(const FString& AssetPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Move an unreferenced generic asset to trash; dry_run=true by default.
	 * @details The effect_asset_soft_delete MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|BatchEffects")
	static FString MCP_EffectAssetSoftDelete(const FString& AssetPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Duplicate an effect-related asset additively.
	 * @details The effect_duplicate_asset MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|BatchEffects")
	static FString MCP_EffectDuplicateAsset(const FString& SourceAssetPath, const FString& NewAssetName, const FString& PackagePath, bool bSaveAssets);

	/**
	 * Discard only an unsaved in-memory duplicate created by MCP_EffectDuplicateAsset.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|BatchEffects")
	static FString MCP_EffectDiscardUnsavedDuplicate(const FString& AssetPath);

	/**
	 * @ProductDocument
	 * @brief Read AMassBattleFxRenderer Blueprint class defaults used by newly placed actors.
	 * @details The batch_fx_read_renderer_defaults MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|BatchEffects")
	static FString MCP_BatchFxReadRendererDefaults(const FString& TargetClassPath);

	/**
	 * @ProductDocument
	 * @brief Set AMassBattleFxRenderer Blueprint CDO defaults for a batched FX subtype.
	 * @details The batch_fx_set_renderer_defaults MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|BatchEffects")
	static FString MCP_BatchFxSetRendererDefaults(const FString& TargetClassPath, const FString& NiagaraSystemPath, const FString& NdcBurstFxPath, int32 SubType, int32 RenderBatchSize, float PoolingCooldown, bool bSaveAssets);
};
