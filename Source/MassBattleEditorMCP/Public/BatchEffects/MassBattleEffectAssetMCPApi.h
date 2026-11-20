// Copyright (c) 2025-2026 Winyunq. All rights reserved.
/**
 * @file MassBattleEffectAssetMCPApi.h
 * @brief Manage Niagara, Cascade, material and Blueprint effect assets and renderer defaults.
 * @details 查询并识别 Niagara、Cascade、材质和蓝图等特效资产，读取类型化摘要或确定性文本，再执行复制、未保存副本回滚或显式删除。渲染器接口读写 MassBattle FX 渲染器类默认值，包括 Niagara 系统、Burst 数据通道、特效子类型和批量大小。
 * 调用者从查询和检查结果取得真实资产路径与配置，再选择对应的资产操作或渲染配置工具。参数、默认行为和 JSON 返回结果在各公开函数中说明。
 */
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MassBattleEffectAssetMCPApi.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMassBattleEffectAssetMCPApi, Log, All);

/**
 * @brief Inspect and copy effect assets and configure batch renderer class defaults.
 * @details These tools intentionally do not perform one-click conversion. They provide
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
	 * @brief Discover effect asset operations available in the connected editor.
	 * @details Lists the asset discovery, typed inspection, deterministic text export, additive duplication and renderer-default primitives used to assemble a MassBattle FX authoring workflow.
	 * @return The effect asset API version and operating model together with supported tools and native parameter definitions.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|BatchEffects")
	static FString MCP_EffectAssetGetApiStatus();

	/**
	 * @ProductDocument
	 * @brief Locate supported effect assets by folder, name and class.
	 * @details Queries supported visual-effect asset classes by path/name text and root. Class filters and recursive_classes control which related asset types are returned for subsequent inspection.
	 * @param[in] QueryJson JSON object with query text, path/root, limit and optional classes array and recursive_classes. Use Unreal class paths for explicit class filters.
	 * @return Matching effect assets and their count, with the effective search root and query.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|BatchEffects")
	static FString MCP_EffectAssetQuery(const FString& QueryJson);

	/**
	 * @ProductDocument
	 * @brief Inspect system, material or Blueprint data for the asset's actual type.
	 * @details Loads the requested visual-effect asset and selects the typed summary appropriate to its actual class. The result supplies asset identity and the class-specific data needed before duplication or conversion.
	 * @param[in] AssetPath Unreal object path of the supported effect-related asset.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. Selects the inspection detail supported by the asset summary implementation.
	 * @return Inspection data selected for the asset's actual type, including its identity and relevant system, material or Blueprint data.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|BatchEffects")
	static FString MCP_EffectAssetReadSummary(const FString& AssetPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Export asset properties and dependencies for comparison.
	 * @details Loads the asset, includes reflected property/dependency information in its typed summary and produces a deterministic text representation for detailed comparison. write_file controls writing the text artifact in addition to returning it.
	 * @param[in] AssetPath Unreal object path of the effect-related asset to export.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. write_file selects whether to write the exported text to disk.
	 * @return The inspected asset summary and comparable text; text_path is included when a file is written.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|BatchEffects")
	static FString MCP_EffectAssetExportText(const FString& AssetPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Check asset references before moving it to the trash folder.
	 * @details Builds a trash-move report, checks hard/soft referencers and blocks a referenced asset unless the explicit corresponding override is supplied. The default dry run returns the proposed destination; an executable request uses AssetTools to move the asset and reports the outcome.
	 * @param[in] AssetPath Unreal object path of the existing effect-related asset.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. dry_run defaults to true; preview_only also requests a preview. trash_package_path/trash_root select the destination. allow_referenced and allow_unsafe_asset_move/force explicitly opt into the supported exceptional moves.
	 * @return A trash-move report: applicable and blocked_reason explain executability, trash_path is the proposed destination, and moved records execution when requested.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|BatchEffects")
	static FString MCP_EffectAssetSoftDelete(const FString& AssetPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Preserve the source effect by creating a copy for conversion.
	 * @details Uses AssetTools to duplicate the source asset into the requested package and name. The duplicate preserves the source-owned visual data. When left unsaved, it is tracked by this editor session so the matching discard tool can roll it back.
	 * @param[in] SourceAssetPath Unreal object path of the existing source effect asset.
	 * @param[in] NewAssetName Name of the new duplicate asset.
	 * @param[in] PackagePath Destination Unreal package folder, such as /Game/Effects.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return The duplicate's object path and package, with saved indicating whether it has been persisted.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|BatchEffects")
	static FString MCP_EffectDuplicateAsset(const FString& SourceAssetPath, const FString& NewAssetName, const FString& PackagePath, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Roll back effect copies left unsaved in this editor session.
	 * @details Discards a duplicate only when this MCP editor session tracked it as unsaved, it is still loaded in a dirty /Game/ package, and it has not crossed the persisted-asset boundary. This supports rolling back a failed conversion before saving.
	 * @param[in] AssetPath Exact object path returned by the duplicate tool for an unsaved duplicate in this editor session.
	 * @return The discarded asset and package, with discarded indicating removal and deleted_count reporting the number removed.
	 * @pre Create the unsaved duplicate with @ref UMassBattleEffectAssetMCPApi::MCP_EffectDuplicateAsset using bSaveAssets=false.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|BatchEffects")
	static FString MCP_EffectDiscardUnsavedDuplicate(const FString& AssetPath);

	/**
	 * @ProductDocument
	 * @brief Inspect effect settings inherited by newly placed renderers.
	 * @details Reads the class-default object of the selected MassBattle FX renderer class. These defaults are the authoring settings inherited by newly placed renderer actors.
	 * @param[in] TargetClassPath Unreal Blueprint/class path resolving to AMassBattleFxRenderer or a derived class.
	 * @return The class defaults in defaults, including its Blueprint/default object, effect system, Burst channel, subtype, batch size and cooldown; unassigned asset references are empty strings.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|BatchEffects")
	static FString MCP_BatchFxReadRendererDefaults(const FString& TargetClassPath);

	/**
	 * @ProductDocument
	 * @brief Set shared effect systems, channels and batch settings for newly placed renderers.
	 * @details Updates the renderer class-default object, recompiles its Blueprint when applicable, marks the package dirty and optionally saves it. Empty system/channel paths keep the existing reference; numeric sentinel values keep the corresponding existing setting.
	 * @param[in] TargetClassPath MassBattle FX renderer Blueprint/class path whose defaults will be edited.
	 * @param[in] NiagaraSystemPath NiagaraSystem object path; an empty string keeps the existing system.
	 * @param[in] NdcBurstFxPath NiagaraDataChannelAsset object path; an empty string keeps the current Burst channel.
	 * @param[in] SubType FX subtype index. A negative value keeps the existing index.
	 * @param[in] RenderBatchSize Positive batch size, or a non-positive value to keep it. The MCP tool default is 2048.
	 * @param[in] PoolingCooldown Pooling cooldown in seconds; a negative value keeps it. The MCP tool default is 3.0.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return The effective renderer class and effect settings after the update, including system, channel, subtype, batch size and cooldown; saved identifies persistence.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|BatchEffects")
	static FString MCP_BatchFxSetRendererDefaults(const FString& TargetClassPath, const FString& NiagaraSystemPath, const FString& NdcBurstFxPath, int32 SubType, int32 RenderBatchSize, float PoolingCooldown, bool bSaveAssets);
};
