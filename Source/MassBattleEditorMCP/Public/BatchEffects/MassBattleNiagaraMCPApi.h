// Copyright (c) 2025-2026 Winyunq. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MassBattleNiagaraMCPApi.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMassBattleNiagaraMCPApi, Log, All);

/**
 * Low-level Niagara MCP tools.
 *
 * These tools intentionally behave like asset/graph command-line primitives:
 * query, read, export text, union-merge write, and explicit delete.
 */
UCLASS()
class MASSBATTLEEDITORMCP_API UMassBattleNiagaraMCPApi : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * @ProductDocument
	 * @brief List Niagara MCP API capabilities.
	 * @details The niagara_get_api_status MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraGetApiStatus();

	/**
	 * @ProductDocument
	 * @brief Query Niagara systems by path or name text.
	 * @details The niagara_query MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraQuery(const FString& QueryJson);

	/**
	 * @ProductDocument
	 * @brief Preflight or execute an explicit source-first Niagara translation manifest; never guesses visual edits.
	 * @details The niagara_batch_translate MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraReadSummary(const FString& SystemPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Read one Niagara module node and pins.
	 * @details The niagara_read_module MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraReadModule(const FString& SystemPath, const FString& SelectorJson);

	/**
	 * Reads one or more Niagara script graphs with stable node/pin ids and explicit links.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraReadGraph(const FString& SystemPath, const FString& SelectorJson);

	/**
	 * Compares a source Niagara system with a duplicate/translation using source-neutral fingerprints.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraCompareSystems(const FString& SourceSystemPath, const FString& TargetSystemPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Read reflected Niagara properties plus every function-call module and pin.
	 * @details The niagara_read_all MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraReadAll(const FString& SystemPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Export deterministic Niagara text for close reading.
	 * @details The niagara_export_text MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraExportText(const FString& SystemPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Union-merge Niagara property writes. This does not delete.
	 * @details The niagara_merge_write MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraMergeWrite(const FString& SystemPath, const FString& PatchJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Set one Niagara function-call module input pin default value.
	 * @details The niagara_set_module_pin MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraSetModulePin(const FString& SystemPath, const FString& SelectorJson, const FString& PinName, const FString& ValueText, bool bSaveAssets);

	/**
	 * Applies an ordered graph-edit plan, compiles once, validates lossless preservation, and optionally saves.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraApplyGraphEdit(const FString& SystemPath, const FString& EditJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Explicitly enable or disable one Niagara emitter handle.
	 * @details The niagara_set_emitter_enabled MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraSetEmitterEnabled(const FString& SystemPath, const FString& EmitterName, bool bEnabled, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Run explicit Niagara delete operations such as renderer or user-parameter removal.
	 * @details The niagara_delete MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraDelete(const FString& SystemPath, const FString& DeleteJson, bool bSaveAssets);

	/**
	 * Adds one configured sprite renderer to an existing emitter without changing its graph.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraAddSpriteRenderer(const FString& SystemPath, const FString& EmitterName, const FString& RendererJson, bool bSaveAssets);
};
