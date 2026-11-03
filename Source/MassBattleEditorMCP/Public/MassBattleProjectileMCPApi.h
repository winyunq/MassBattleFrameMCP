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
	 * @details The projectile_get_api_status MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileGetApiStatus();

	/**
	 * @ProductDocument
	 * @brief List MassBattle projectile configuration DataAssets.
	 * @details The projectile_list MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileList(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Query MassBattle projectile configuration DataAssets by path or name.
	 * @details The projectile_query MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileQuery(const FString& QueryJson);

	/**
	 * @ProductDocument
	 * @brief Read one projectile DataAsset in active-only or full source-aligned form.
	 * @details The projectile_get MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileGet(const FString& ProjectilePath, const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Read projectile field types, enums, conditions, tooltips, and write rules.
	 * @details The projectile_get_schema MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileGetSchema(const FString& OptionsJson);

	/**
	 * @ProductDocument
	 * @brief Create a projectile DataAsset from a template or class defaults.
	 * @details The projectile_create MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileCreate(const FString& CreateSpecJson, bool bSaveAssets = true);

	/**
	 * @ProductDocument
	 * @brief Union-write source-aligned projectile data after transient preflight and validation.
	 * @details The projectile_write MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileWrite(const FString& ProjectilePath, const FString& PatchJson, bool bSaveAssets = true);

	/**
	 * @ProductDocument
	 * @brief Validate movement, damage ownership, triggers, lifecycle FX, and duplicate explosions.
	 * @details The projectile_validate MCP tool invokes this editor entry and returns its JSON response.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileValidate(const FString& ProjectilePath, const FString& OptionsJson);

	/**
	 * Delete is a dry run unless OptionsJson contains dry_run=false.
	 *
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "MassBattleEditorMCP|Projectile")
	static FString MCP_ProjectileDelete(const FString& ProjectilePath, const FString& OptionsJson);
};
