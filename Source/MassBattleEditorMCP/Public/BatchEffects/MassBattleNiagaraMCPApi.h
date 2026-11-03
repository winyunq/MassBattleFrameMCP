// Copyright (c) 2025-2026 Winyunq. All rights reserved.
/**
 * @file MassBattleNiagaraMCPApi.h
 * @brief Inspect and edit Niagara systems for MassBattle batch effect conversion.
 * @details 以 UE 5.8 的 Niagara 编辑器能力为完整功能基线，读取系统、发射器、渲染器、模块引脚和脚本图，再通过显式属性写入、堆栈输入与图编辑把现有效果接入 MassBattle 批处理。
 * 改造过程使用源系统与目标系统的语义比较、图连接证据和编译诊断检查保留结果；写入和删除分别使用各自的公开工具。各函数说明其选择器、操作 JSON、保存行为及返回报告。
 */
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MassBattleNiagaraMCPApi.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMassBattleNiagaraMCPApi, Log, All);

/**
 * @brief Inspect and edit effect emitters, modules, parameters and graph connections.
 * @details These tools intentionally behave like asset/graph command-line primitives:
 * query, read, export text, union-merge write, and explicit delete.
 */
UCLASS()
class MASSBATTLEEDITORMCP_API UMassBattleNiagaraMCPApi : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * @ProductDocument
	 * @brief Discover Niagara query and editing operations in the connected editor.
	 * @details Returns native Niagara tool capabilities and parameter names for querying, inspecting, comparing and editing existing systems. The entries are graph/property primitives used to construct an explicit authoring workflow.
	 * @return The Niagara API version and operating model, together with supported inspection and graph-editing tools.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraGetApiStatus();

	/**
	 * @ProductDocument
	 * @brief Locate Niagara systems by name or folder.
	 * @details Scans NiagaraSystem assets under the requested root, filters by path/name text and sorts candidates by object path. matched counts all query matches and returned records the bounded result count.
	 * @param[in] QueryJson JSON object with query text, path/root and limit.
	 * @return The matching systems' names and paths; matched is the total match count and returned is the count after applying the result limit.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraQuery(const FString& QueryJson);

	/**
	 * @ProductDocument
	 * @brief Inspect the system's emitters, renderers and user parameters.
	 * @details Loads the Niagara system and summarizes emitters, renderers, user parameters and compile/authoring information. include_modules controls whether function-call module summaries are included.
	 * @param[in] SystemPath Unreal object path of the NiagaraSystem asset.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. include_modules selects function-call module summaries.
	 * @return An emitter, renderer, user-parameter and compile summary, with module summaries included when include_modules is enabled.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraReadSummary(const FString& SystemPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Identify a module and its pins before editing them.
	 * @details Collects the system function-call module nodes and returns the first record matching the provided emitter/module/GUID/index filters. The module record includes its pins for a precise later pin edit.
	 * @param[in] SystemPath Unreal object path of the NiagaraSystem asset.
	 * @param[in] SelectorJson JSON object with optional emitter, module/function_name, node_guid and module_index/index. Use a GUID or collected module index to identify a specific matching node.
	 * @return The selected module and its pins for subsequent inspection or editing; a selector with no matching node produces an error.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraReadModule(const FString& SystemPath, const FString& SelectorJson);

	/**
	 * @ProductDocument
	 * @brief Obtain node, pin and link IDs for precise graph edits.
	 * @details Reads system or standalone NiagaraScript graphs and returns stable node/pin identifiers and explicit links. Optional stack-input topology and compile-state reports provide the identities and diagnostics used by the graph-edit tool.
	 * @param[in] SystemPath Unreal object path of a NiagaraSystem or supported NiagaraScript asset.
	 * @param[in] SelectorJson JSON object; pass {} to use the defaults. include_nodes, include_pins, include_stack_inputs and include_compile_state select returned detail; max_nodes bounds exported graph nodes. Graph selection fields choose the relevant script/emitter scope.
	 * @return Script graphs in the selected scope; node, pin, connection, stack-input and compile details are included according to the selectors.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraReadGraph(const FString& SystemPath, const FString& SelectorJson);

	/**
	 * @ProductDocument
	 * @brief Check whether a copy or conversion preserves the source system's structure.
	 * @details Loads two different systems and compares source-neutral semantic fingerprints. exact mode checks a duplicate; translation mode applies the configured preservation rules for an intended conversion. Compile readiness/error/warning options control the corresponding result checks.
	 * @param[in] SourceSystemPath Unreal object path of the reference NiagaraSystem.
	 * @param[in] TargetSystemPath Unreal object path of a different NiagaraSystem to compare against the source.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. mode must be exact or translation. require_ready_to_run, require_no_compile_errors and require_no_warnings select compile-state requirements.
	 * @return Source-preservation findings, source/target emitter and renderer counts, unexpected removed links and compile readiness.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraCompareSystems(const FString& SourceSystemPath, const FString& TargetSystemPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Inspect system and emitter properties together with module pin values.
	 * @details Combines the system summary with reflected system/emitter-data properties and every function-call module and pin. This provides the current source-aligned values before forming a property or pin edit.
	 * @param[in] SystemPath Unreal object path of the NiagaraSystem asset.
	 * @param[in] OptionsJson JSON object; pass {} to use the defaults. max_properties bounds reflected properties exported for each inspected object.
	 * @return The system summary, reflected system/emitter properties and detailed function-call modules and pins.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraReadAll(const FString& SystemPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Produce comparable text describing Niagara system structure.
	 * @details Builds a deterministic text representation of the system summary and detailed modules for close reading and comparison. write_file selects whether the returned text is also written to disk.
	 * @param[in] SystemPath Unreal object path of the existing NiagaraSystem.
	 * @param[in] OptionsJson JSON object; pass {} for defaults. write_file selects file output.
	 * @return The system summary, detailed modules and corresponding structure text, with text_path when saved to a file.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraExportText(const FString& SystemPath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Patch system, emitter or renderer properties while retaining other values.
	 * @details Resolves explicit system, emitter-data or renderer targets and union-merges requested reflected property values. Omitted properties are retained. Each result records target resolution and before/after values; saving is optional.
	 * @param[in] SystemPath Unreal object path of the existing NiagaraSystem.
	 * @param[in] PatchJson JSON containing patches, or one patch object. Each patch selects a target, property/path and value_text or value. Use real reflected property names from read_all.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return Per-patch results identifying resolved targets, before/after values and saved state, with errors on failed patches.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraMergeWrite(const FString& SystemPath, const FString& PatchJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Use a node or module selector to adjust an input pin's default.
	 * @details Resolves one function-call module and named pin, then writes its default value. Linked pins require allow_linked=true because the default may be ignored; non-input pins require allow_output=true. The report identifies the selected node and old/new text.
	 * @param[in] SystemPath Unreal object path of the existing NiagaraSystem.
	 * @param[in] SelectorJson JSON using emitter, module/function_name, node_guid or module_index; allow_linked and allow_output opt into the corresponding writes.
	 * @param[in] PinName Exact pin name returned by module inspection.
	 * @param[in] ValueText Default value in the selected pin type text/import syntax.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return The selected emitter, module and pin, its before/after defaults, existing connection count and saved state.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraSetModulePin(const FString& SystemPath, const FString& SelectorJson, const FString& PinName, const FString& ValueText, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Batch-edit graph connections and stacks, then check conversion results.
	 * @details Validates ordered operation records and ids, executes explicit graph/stack edits, compiles once and checks source preservation before saving. Operations include emitter copying, user values/data interfaces, module/assignment insertion, pin connection/disconnection/rewiring and stack-input edits. Empty operations requests compile/validation only.
	 * @param[in] SystemPath Unreal object path of the existing NiagaraSystem.
	 * @param[in] EditJson JSON with an operations array (alias edits). Each object supplies op/type, optional unique id and the fields required for its operation and source-preservation policy.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return Ordered edit outcomes, compile diagnostics, source-preservation findings and saved state; failures report the operation or validation error.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraApplyGraphEdit(const FString& SystemPath, const FString& EditJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Switch an emitter's participation and report the actual state change.
	 * @details Finds the named emitter handle, sets its enabled state explicitly, records whether it changed and optionally saves the system.
	 * @param[in] SystemPath Unreal object path of the existing NiagaraSystem.
	 * @param[in] EmitterName Name of the existing emitter handle.
	 * @param[in] bEnabled Desired enabled state of the emitter.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return The emitter's before/after enabled state; changed identifies an actual state change and saved identifies persistence.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraSetEmitterEnabled(const FString& SystemPath, const FString& EmitterName, bool bEnabled, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Remove renderers or user parameters, or disable an emitter.
	 * @details Executes explicit renderer removal, user-parameter removal or emitter disabling on the selected system. Each action identifies its target and reports the actual removed/changed result.
	 * @param[in] SystemPath Unreal object path of the existing NiagaraSystem.
	 * @param[in] DeleteJson JSON selecting type=renderer, user_parameter or disable_emitter. Renderer removal selects emitter and renderer_index/index; user_parameter removal requires name; disabling names an emitter.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return The processed target and its removal or disable result, including actual removed/changed state and persistence.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraDelete(const FString& SystemPath, const FString& DeleteJson, bool bSaveAssets);

	/**
	 * @ProductDocument
	 * @brief Add a sprite renderer with material and visibility bindings to an existing emitter.
	 * @details Allocates and configures a sprite renderer on an existing emitter, including its material, user-parameter bindings, sort order and visibility. It adds the renderer without rewriting the emitter graph, then finishes compilation and optional saving.
	 * @param[in] SystemPath Unreal object path of the existing NiagaraSystem.
	 * @param[in] EmitterName Name of the existing emitter receiving the new renderer.
	 * @param[in] RendererJson JSON with material and optional material_user_parameter, enabled_user_parameter, sort_order and renderer_visibility.
	 * @param[in] bSaveAssets Whether to save the assets modified or created by this operation to disk.
	 * @return The new renderer's add/compile report and persistence result; failures identify target, material or creation problems.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Niagara")
	static FString MCP_NiagaraAddSpriteRenderer(const FString& SystemPath, const FString& EmitterName, const FString& RendererJson, bool bSaveAssets);
};
