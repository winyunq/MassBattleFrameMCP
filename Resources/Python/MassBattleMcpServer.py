import asyncio
import json
import logging
import sys
from contextlib import asynccontextmanager
from typing import Any, AsyncIterator, Dict, Optional

from mcp.server.fastmcp import FastMCP

from mcp_config import COMMAND_TIMEOUT, CONNECT_TIMEOUT, UNREAL_HOST, UNREAL_PORT

try:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
except Exception:
    pass

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    handlers=[logging.FileHandler("massbattle_mcp_server.log")],
)
logger = logging.getLogger("MassBattleMcpServer")


def _json_arg(value: Any, default: str = "{}") -> str:
    if value is None:
        return default
    if isinstance(value, str):
        return value if value.strip() else default
    return json.dumps(value, ensure_ascii=False)


def _json_arg_with_defaults(value: Any, defaults: Dict[str, Any]) -> str:
    if value is None:
        return json.dumps(defaults, ensure_ascii=False)
    if isinstance(value, dict):
        merged = dict(defaults)
        merged.update(value)
        return json.dumps(merged, ensure_ascii=False)
    if isinstance(value, str):
        if not value.strip():
            return json.dumps(defaults, ensure_ascii=False)
        try:
            parsed = json.loads(value)
        except Exception:
            return value
        if isinstance(parsed, dict):
            merged = dict(defaults)
            merged.update(parsed)
            return json.dumps(merged, ensure_ascii=False)
        return value
    return _json_arg(value)


class UnrealConnection:
    async def send_command(self, command: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        try:
            reader, writer = await asyncio.wait_for(
                asyncio.open_connection(UNREAL_HOST, UNREAL_PORT),
                timeout=CONNECT_TIMEOUT,
            )
        except Exception as exc:
            return {
                "success": False,
                "error": f"Could not connect to MassBattleEditorMCP bridge at {UNREAL_HOST}:{UNREAL_PORT}: {exc}",
            }

        try:
            message = json.dumps({"command": command, "params": params or {}}, ensure_ascii=False)
            writer.write(message.encode("utf-8") + b"\0")
            await writer.drain()
            if writer.can_write_eof():
                writer.write_eof()

            chunks = []
            while True:
                chunk = await asyncio.wait_for(reader.read(4096), timeout=COMMAND_TIMEOUT)
                if not chunk:
                    break
                if b"\0" in chunk:
                    chunks.append(chunk[: chunk.find(b"\0")])
                    break
                chunks.append(chunk)

            payload = b"".join(chunks).decode("utf-8")
            if not payload:
                return {"success": False, "error": "Empty response from Unreal MassBattleEditorMCP bridge."}
            return json.loads(payload)
        except Exception as exc:
            logger.exception("MassBattle MCP command failed: %s", command)
            return {"success": False, "error": str(exc), "command": command}
        finally:
            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass


_unreal_connection: Optional[UnrealConnection] = None


def get_connection() -> UnrealConnection:
    global _unreal_connection
    if _unreal_connection is None:
        _unreal_connection = UnrealConnection()
    return _unreal_connection


@asynccontextmanager
async def server_lifespan(server: FastMCP) -> AsyncIterator[Dict[str, Any]]:
    get_connection()
    yield {}


mcp = FastMCP("MassBattleEditorMCP", lifespan=server_lifespan)


@mcp.tool()
async def massbattle_ping() -> Dict[str, Any]:
    """Check whether the Unreal MassBattleEditorMCP bridge is reachable."""
    return await get_connection().send_command("ping")


@mcp.tool()
async def unit_get_api_status() -> Dict[str, Any]:
    """List Unit MCP API capabilities from Unreal.
    MCP tool: `unit_get_api_status`. Returns the native unit API identity and categorized tools with their descriptions. This is capability discovery and does not load or modify a unit asset.
    
    Returns: JSON with success, api_name, module, dependency_note and categorized tool metadata."""
    return await get_connection().send_command("MCP_UnitGetApiStatus")


@mcp.tool()
async def unit_list(options: Any = None) -> Dict[str, Any]:
    """List MassBattle unit config assets.
    MCP tool: `unit_list`. Scans unit config assets, filters their object paths by query, and returns compact balance summaries. load_fields controls whether assets are loaded for authored values. The returned total_scanned counts candidates before the query and result limit.
    OptionsJson: JSON object; pass {} to use the defaults. query is case-insensitive path text; limit is clamped to 1..2000; load_fields defaults to true.
    Returns: JSON with success, count, total_scanned, units, roots and default_ignore_policy. Invalid JSON returns success=false with error."""
    return await get_connection().send_command("MCP_UnitList", {"OptionsJson": _json_arg(options)})


@mcp.tool()
async def unit_get(unit_path: str, options: Any = None) -> Dict[str, Any]:
    """Read a MassBattle unit config asset.
    MCP tool: `unit_get`. Loads the requested unit or Unit Source Actor and returns source-aligned authored Data. The default simple view omits runtime, system and deprecated fields; a source Actor response also identifies export_path, exported_unit and last_report.
    UnitPath: Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
    OptionsJson: JSON object; pass {} to use the defaults. Select the requested detail/object view and field inclusion policy; the response reports detail and default_ignore_policy.
    Returns: JSON with success, AssetName, ObjectPath, Class, detail and Data. Invalid JSON or an unresolved unit returns success=false and error."""
    return await get_connection().send_command(
        "MCP_UnitGet",
        {"UnitPath": unit_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def unit_get_schema(options: Any = None) -> Dict[str, Any]:
    """Read editable MassBattle unit schema information.
    MCP tool: `unit_get_schema`. Builds the editable property schema of UMassBattleAgentConfigDataAsset and applies the same default field-ignore policy as unit reads. Recursive mode expands nested struct fields so a caller can build a patch from real property names.
    OptionsJson: JSON object; pass {} to use the defaults. recursive defaults to true; depth defaults to 3 in recursive mode and 0 otherwise, and is clamped to 0..8.
    Returns: JSON with success, asset_class, fields and default_ignore_policy. Invalid JSON returns success=false and error."""
    return await get_connection().send_command("MCP_UnitGetSchema", {"OptionsJson": _json_arg(options)})


@mcp.tool()
async def unit_export(options: Any = None) -> Dict[str, Any]:
    """Export a compact MassBattle unit balance table to JSON or CSV.
    MCP tool: `unit_export`. Reads the selected unit balance fields and writes a JSON or CSV table. With no output_path, files are placed under the project Saved/MassBattleEditorMCP/Exports directory. The response identifies the created file and exported unit count.
    OptionsJson: JSON object; pass {} to use the defaults. format chooses JSON or CSV; output_path selects the destination file. Unit scan and field options choose table contents.
    Returns: JSON with success, format, output_path and unit_count. Invalid options or a file-save failure returns success=false and error."""
    return await get_connection().send_command("MCP_UnitExport", {"OptionsJson": _json_arg(options)})


@mcp.tool()
async def unit_write(unit_path: str, unit_data: Any, save_assets: bool = True) -> Dict[str, Any]:
    """Union-write partial MassBattle unit data to an existing unit.
    MCP tool: `unit_write`. Plans a union-merge, checks success and applicable, then applies the returned plan automatically. It retains omitted fields and returns the planning result when validation blocks application.
    UnitPath: Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
    UnitDataJson: Partial source-aligned authored unit data. Obtain exact editable field names from the unit schema.
    bSaveAssets: Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
    Returns: The planning error or the apply response, including success, saved and applied_diff.
    UMassBattleUnitMCPApi::MCP_UnitGetSchema for editable fields."""
    return await get_connection().send_command(
        "MCP_UnitMergeUpdate",
        {"UnitPath": unit_path, "UnitDataJson": _json_arg(unit_data), "bSaveAssets": save_assets},
    )


@mcp.tool()
async def unit_create(create_spec: Any, save_assets: bool = True) -> Dict[str, Any]:
    """Create a MassBattle unit from the default or specified template.
    MCP tool: `unit_create`. Creates a unit from the default or explicit template and optionally merges initial authored data. asset_type=source_actor creates a Unit Source Actor Blueprint; the ordinary branch creates the unit config. Target names must resolve to an unoccupied Unreal asset path.
    CreateSpecJson: JSON with either unit_path/new_unit_path or both asset_name and package_path. template_unit selects a template; unit_data supplies initial authored values. asset_type=source_actor enables the source Blueprint branch and export_path configures its output.
    bSaveAssets: Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
    Returns: The create/apply JSON response identifying the created asset and saved state. Invalid target paths, occupied assets or initial-data merge errors return success=false and error."""
    return await get_connection().send_command(
        "MCP_UnitCreate",
        {"CreateSpecJson": _json_arg(create_spec), "bSaveAssets": save_assets},
    )


@mcp.tool()
async def unit_delete(unit_path: str, options: Any = None) -> Dict[str, Any]:
    """Delete or soft-delete a MassBattle unit; options default to dry_run=true.
    MCP tool: `unit_delete`. Builds the explicit unit deletion plan. dry_run=true returns would_delete and the plan without changing the asset; dry_run=false applies an applicable plan through the same mutation API.
    UnitPath: Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
    OptionsJson: JSON object; pass {} to use the defaults. dry_run defaults to true. Deletion mode and plan validation are handled by the explicit unit deletion plan.
    Returns: A deletion plan with dry_run and would_delete, or the applied deletion result. Invalid JSON or an inapplicable plan reports success=false/error or applicable=false."""
    return await get_connection().send_command(
        "MCP_UnitDelete",
        {"UnitPath": unit_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def unit_find_assets(query: Any) -> Dict[str, Any]:
    """Find project assets useful for MassBattle unit authoring.
    MCP tool: `unit_find_assets`. Searches meshes, renderers, Niagara systems, AnimToTexture assets and existing unit configs, then sorts matches by object path for stable authoring discovery.
    QueryJson: JSON query object containing query text and limit. Results identify asset name, path, class, package_path and inferred style_family.
    Returns: JSON with success, count and assets. Invalid query JSON returns success=false and error."""
    return await get_connection().send_command("MCP_UnitFindAssets", {"QueryJson": _json_arg(query)})


@mcp.tool()
async def projectile_get_api_status() -> Dict[str, Any]:
    """List projectile DataAsset CRUD, schema, and validation capabilities.
    MCP tool: `projectile_get_api_status`. Returns projectile DataAsset capabilities, native parameter names and write-contract metadata. It identifies the schema, CRUD and validation entries available to the connected editor.
    
    Returns: JSON with success, api_name, version, asset_class, write_contract and tools."""
    return await get_connection().send_command("MCP_ProjectileGetApiStatus")


@mcp.tool()
async def projectile_list(options: Any = None) -> Dict[str, Any]:
    """List MassBattle projectile configuration DataAssets.
    MCP tool: `projectile_list`. Scans projectile configuration DataAssets, filters object paths by query and returns stable asset summaries. load_fields selects whether authored fields are loaded for each result.
    OptionsJson: JSON object; pass {} to use the defaults. query filters paths; limit bounds the result count; load_fields controls summary detail. Scan roots select the content scope.
    Returns: JSON with success, count, total_scanned, roots and projectiles, or success=false/error for invalid options."""
    return await get_connection().send_command(
        "MCP_ProjectileList",
        {"OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def projectile_query(query: Any = None) -> Dict[str, Any]:
    """Query MassBattle projectile configuration DataAssets by path or name.
    MCP tool: `projectile_query`. Uses the projectile listing query path to find configurations by path or name. It returns the same stable summaries and scan metadata as the projectile list entry.
    QueryJson: JSON object; pass {} to use the defaults. Uses the projectile-list query and scope options.
    Returns: The projectile-list response with success, counts, roots and matching projectiles."""
    return await get_connection().send_command(
        "MCP_ProjectileQuery",
        {"QueryJson": _json_arg(query)},
    )


@mcp.tool()
async def projectile_get(projectile_path: str, options: Any = None) -> Dict[str, Any]:
    """Read one projectile DataAsset in active-only or full source-aligned form.
    MCP tool: `projectile_get`. Loads one projectile DataAsset and returns source-aligned authored data. The active view selects fields relevant to the configured movement and damage modes; the full view can include inactive fields. Validation and referencing assets can be included in the same read.
    ProjectilePath: Unreal object path of the projectile configuration DataAsset.
    OptionsJson: JSON object; pass {} to use the defaults. view/detail and include_inactive select the field view; include_validation and include_referencers request the corresponding reports.
    Returns: JSON with success, asset_name, object_path, class, view, movement_mode, damage_mode and data, plus requested validation/referencers."""
    return await get_connection().send_command(
        "MCP_ProjectileGet",
        {"ProjectilePath": projectile_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def projectile_get_schema(options: Any = None) -> Dict[str, Any]:
    """Read projectile field types, enums, conditions, tooltips, and write rules.
    MCP tool: `projectile_get_schema`. Reflects projectile field names, types, enums, edit conditions and tooltips. It returns movement/damage-mode field groups and the source-aligned write, damage-ownership and lifecycle rules needed to form a valid patch. Array writes merge by index and reject appending by default; options.append_arrays=true permits append and options.array_mode=replace requests replacement. The returned ownership rule assigns normal projectile attack damage to the projectile DataAsset with Agent Attack.TimeOfHitAction=None; the lifecycle rule identifies duplicate full explosions in OnHit and OnRemoval.
    OptionsJson: JSON object; pass {} to use the defaults. depth selects the nested schema expansion depth.
    Returns: JSON with success, class, field_count, fields, movement_mode_fields, damage_mode_fields, array_write_rule, damage_ownership_rule and lifecycle_rule."""
    return await get_connection().send_command(
        "MCP_ProjectileGetSchema",
        {"OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def projectile_create(create_spec: Any, save_assets: bool = True) -> Dict[str, Any]:
    """Create a projectile DataAsset from a template or class defaults.
    MCP tool: `projectile_create`. Builds a transient preview from the requested template or class defaults, applies initial source-aligned data and validates it before creating the real projectile DataAsset. It rejects an invalid or occupied destination instead of replacing that asset.
    CreateSpecJson: JSON with asset_name (alias name), package_path (alias target_package_path) and optional template_path/template plus initial projectile data. package_path must be an Unreal package path beginning with /.
    bSaveAssets: Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
    Returns: JSON with success, asset_path, template_path, saved, changed_paths and data. Invalid input, preflight failures or asset creation errors return success=false/error."""
    return await get_connection().send_command(
        "MCP_ProjectileCreate",
        {"CreateSpecJson": _json_arg(create_spec), "bSaveAssets": save_assets},
    )


@mcp.tool()
async def projectile_write(
    projectile_path: str,
    patch: Any,
    save_assets: bool = True,
) -> Dict[str, Any]:
    """Union-write source-aligned projectile data after transient preflight and validation.
    MCP tool: `projectile_write`. Applies the partial source-aligned patch to a transient copy and validates the candidate before mutating the real projectile configuration. Omitted fields are retained; use the schema write rules when changing arrays or nested authored structs.
    ProjectilePath: Unreal object path of the existing projectile DataAsset.
    PatchJson: Nonempty partial projectile JSON using real source field names. Read the projectile schema for enum values, conditions and array write rules. options.append_arrays and options.array_mode explicitly select array append/replacement behavior.
    bSaveAssets: Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
    Returns: JSON with success, asset_path, saved, changed_paths, data and validation, or success=false/error for invalid fields, failed preflight or saving.
    UMassBattleProjectileMCPApi::MCP_ProjectileGetSchema for exact field/write rules."""
    return await get_connection().send_command(
        "MCP_ProjectileWrite",
        {
            "ProjectilePath": projectile_path,
            "PatchJson": _json_arg(patch),
            "bSaveAssets": save_assets,
        },
    )


@mcp.tool()
async def projectile_validate(projectile_path: str, options: Any = None) -> Dict[str, Any]:
    """Validate movement, damage ownership, triggers, lifecycle FX, and duplicate explosions.
    MCP tool: `projectile_validate`. Checks movement configuration, damage ownership, trigger/lifetime behavior, lifecycle effects and duplicate explosion risks on the requested projectile. It does not rewrite the configuration.
    ProjectilePath: Unreal object path of the projectile DataAsset to inspect.
    OptionsJson: JSON object; pass {} to use the defaults. include_referencers includes assets that reference the projectile.
    Returns: JSON with success and validation, plus requested referencers. An invalid path or options object returns success=false/error."""
    return await get_connection().send_command(
        "MCP_ProjectileValidate",
        {"ProjectilePath": projectile_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def projectile_delete(projectile_path: str, options: Any = None) -> Dict[str, Any]:
    """Plan or delete a projectile DataAsset; dry_run=true by default.
    MCP tool: `projectile_delete`. Builds a deletion/trash move report and checks references. dry_run=true returns the report without changing the DataAsset; an explicit executable request performs the selected supported mode and returns the actual move/delete outcome.
    ProjectilePath: Unreal object path of the projectile configuration to remove.
    OptionsJson: JSON object; pass {} to use the defaults. dry_run defaults to true. mode chooses the supported deletion path; trash_root selects trash placement. Referenced/unsafe/hard operations require their corresponding allow_referenced, allow_unsafe_asset_move or allow_hard_delete option.
    Returns: JSON with dry_run, mode, source_path, referencers, applicable and blocked_reason; executed requests add moved/trash_path or deleted/deleted_count and any error."""
    return await get_connection().send_command(
        "MCP_ProjectileDelete",
        {"ProjectilePath": projectile_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def style_summarize_units(options: Any = None) -> Dict[str, Any]:
    """Summarize MassBattle unit organization by style, family, and path category.
    MCP tool: `style_summarize_units`. Scans the selected unit assets and aggregates their authored style indices, inferred style families and path categories. It reports organization counts without moving assets.
    OptionsJson: JSON object; pass {} to use the defaults. Use the unit scan options to choose the assets included in the summary.
    Returns: JSON with success, unit_count, style_families, path_categories and style_indices."""
    return await get_connection().send_command("MCP_StyleSummarizeUnits", {"OptionsJson": _json_arg(options)})


@mcp.tool()
async def style_plan_organize_units(options: Any = None) -> Dict[str, Any]:
    """Plan style-based MassBattle unit folder organization without moving assets.
    MCP tool: `style_plan_organize_units`. Calculates destination folders from each unit style family and style index. It produces a reviewable move list only; this entry does not execute AssetTools moves.
    OptionsJson: JSON object; pass {} to use the defaults. target_root selects the organization root; unit scan options select the source assets.
    Returns: JSON with success, plan_type, target_root, dry_run_only=true and moves containing source and destination paths."""
    return await get_connection().send_command("MCP_StylePlanOrganizeUnits", {"OptionsJson": _json_arg(options)})


@mcp.tool()
async def editor_get_status() -> Dict[str, Any]:
    """List MassBattle unit editor workflow capabilities.
    MCP tool: `editor_get_status`. Returns categorized capabilities for the non-UI counterpart of the MassBattleTools editor widget. The list describes available profile, VAT, Actor assembly and organization workflows without executing them.
    
    Returns: JSON with success, api_name, description and categorized tools."""
    return await get_connection().send_command("MCP_EditorGetStatus")


@mcp.tool()
async def editor_list_profiles(options: Any = None) -> Dict[str, Any]:
    """List MassBattle unit style profiles and authoring recipes.
    MCP tool: `editor_list_profiles`. Reads the installed unit style profiles and authoring recipes so a caller can choose an existing profile id before constructing a VAT or organization spec.
    OptionsJson: JSON object; pass {} to use the defaults. This entry parses the options object before enumerating installed profiles.
    Returns: JSON with success and profiles. Malformed options returns success=false and error."""
    return await get_connection().send_command("MCP_EditorListProfiles", {"OptionsJson": _json_arg(options)})


@mcp.tool()
async def editor_get_profile(profile_type: str, profile_id: str) -> Dict[str, Any]:
    """Read one MassBattle unit style profile or recipe.
    MCP tool: `editor_get_profile`. Resolves a style or recipe profile by its type and id, reads its JSON file and returns both the file identity and complete profile contents.
    ProfileType: Profile kind: style or recipe.
    ProfileId: Existing profile id returned by the profile-list tool.
    Returns: JSON with success, file and profile. A missing profile or invalid profile JSON returns success=false and error.
    UMassBattleUnitEditorMCPApi::MCP_EditorListProfiles to discover ids."""
    return await get_connection().send_command(
        "MCP_EditorGetProfile",
        {"ProfileType": profile_type, "ProfileId": profile_id},
    )


@mcp.tool()
async def editor_plan_create_vat_unit(spec: Any) -> Dict[str, Any]:
    """Diagnostic: preview the MassBattleTools DoAll-equivalent VAT unit plan; strict apply still requires complete canonical inputs.
    MCP tool: `editor_plan_create_vat_unit`. Resolves source mesh, recipe/style settings, animation and texture discovery, and generated-asset layout without writing generated assets. This is a diagnostic preview; its discovery aliases or inferred values do not replace the complete canonical fields required by strict apply.
    SpecJson: JSON describing the source skeletal_mesh and unit_name, discovery paths, optional recipe_id/style_profile and intended generated asset layout. Use canonical fields skeletal_mesh, unit_name, target_package_path, parent_material, source_renderer_class, niagara_system, vat_sample_rate and a nonempty animations category-to-AnimSequence mapping. Supply target_unit to update a unit, or template_unit, target_unit_package_path and subtype to create one. Strict execution rejects legacy aliases and missing-asset/static fallback switches.
    Returns: The planning JSON with resolved_layout, discovery, applicability, warnings and proposed steps, or success=false and error for invalid input/profile resolution.
    UMassBattleUnitEditorMCPApi::MCP_EditorApplyCreateVatUnit for the primary generation entry."""
    return await get_connection().send_command("MCP_EditorPlanCreateVatUnit", {"SpecJson": _json_arg(spec)})


@mcp.tool()
async def editor_validate_create_vat_unit(spec: Any) -> Dict[str, Any]:
    """Diagnostic: validate DoAll-equivalent VAT unit inputs without writing assets.
    MCP tool: `editor_validate_create_vat_unit`. Checks the strict canonical VAT contract, resolves referenced assets and generates a diagnostic execution preview. It reports field-level issues and does not create, bake or save assets.
    SpecJson: Complete canonical VAT generation JSON. Use canonical fields skeletal_mesh, unit_name, target_package_path, parent_material, source_renderer_class, niagara_system, vat_sample_rate and a nonempty animations category-to-AnimSequence mapping. Supply target_unit to update a unit, or template_unit, target_unit_package_path and subtype to create one. Strict execution rejects legacy aliases and missing-asset/static fallback switches.
    Returns: JSON with success, validation_type=create_vat_unit, valid, issues and execution_preview. Check valid before interpreting the workflow as executable."""
    return await get_connection().send_command("MCP_EditorValidateCreateVatUnit", {"SpecJson": _json_arg(spec)})


@mcp.tool()
async def editor_apply_create_vat_unit(
    spec: Any,
    save_assets: bool = True,
    compact_response: bool = True,
) -> Dict[str, Any]:
    """Primary non-selection DoAll-equivalent VAT unit authoring entry; requires canonical complete inputs and validates before writing assets.
    MCP tool: `editor_apply_create_vat_unit`. Validates complete canonical inputs before running the MassBattleTools DoAll-equivalent skeletal-mesh VAT workflow. It creates or updates the required VAT data, materials, renderer and unit through MassBattleEditor. dry_run/preview_only requests validation and execution preview instead of asset writes. The MCP tool defaults compact_response to true; an explicit value in spec overrides that default.
    SpecJson: Complete canonical VAT generation JSON. Use canonical fields skeletal_mesh, unit_name, target_package_path, parent_material, source_renderer_class, niagara_system, vat_sample_rate and a nonempty animations category-to-AnimSequence mapping. Supply target_unit to update a unit, or template_unit, target_unit_package_path and subtype to create one. Strict execution rejects legacy aliases and missing-asset/static fallback switches. Optional dry_run/preview_only controls preview, overwrite_existing controls existing generated outputs, and compact_response selects compact results.
    bSaveAssets: Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
    Returns: JSON with success, dry_run, save_assets and execution_steps or a compact apply summary. Validation failures include valid=false, issues and execution_preview; step failures return their actual result.
    Validation is performed automatically; diagnostic plan/validate calls are optional before this primary entry."""
    return await get_connection().send_command(
        "MCP_EditorApplyCreateVatUnit",
        {"SpecJson": _json_arg_with_defaults(spec, {"compact_response": compact_response}), "bSaveAssets": save_assets},
    )


@mcp.tool()
async def unit_plan_write(unit_path: str, unit_data: Any) -> Dict[str, Any]:
    """Plan a union-write and return its diff without mutating the unit.
    MCP tool: `unit_plan_write`. Builds and stores a union-merge plan from a partial source-aligned unit object. Omitted unit fields are retained. The asset is not modified; applicable and merge_errors describe whether the planned field changes can be applied.
    UnitPath: Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
    UnitDataJson: Partial unit JSON using source property names. Include the fields to change; a Unit Source Actor patch may also contain export_path.
    Returns: JSON with plan_id, plan_path, merge_semantics, patches, diff, merge_errors and applicable. Invalid JSON, an unresolved asset or no mergeable fields returns success=false and error."""
    return await get_connection().send_command(
        "MCP_UnitPlanMergeUpdate",
        {"UnitPath": unit_path, "UnitDataJson": _json_arg(unit_data)},
    )


@mcp.tool()
async def unit_preview_plan(plan_id: str) -> Dict[str, Any]:
    """Read a previously created unit mutation plan and its complete diff.
    MCP tool: `unit_preview_plan`. Loads a previously stored mutation plan and returns its complete diff and applicability without applying it. This is the review step between planning and an explicit apply.
    PlanId: plan_id returned by a unit planning operation.
    Returns: JSON with success, plan_id, type, applicable, diff/diff_from_template and the complete plan. A missing or unreadable plan returns success=false and error.
    Before calling: Obtain PlanId from a planning operation such as UMassBattleUnitMCPApi::MCP_UnitPlanMergeUpdate."""
    return await get_connection().send_command(
        "MCP_UnitPreviewDiff",
        {"PlanId": plan_id},
    )


@mcp.tool()
async def unit_apply_plan(plan_id: str, save_assets: bool = True) -> Dict[str, Any]:
    """Apply a reviewed unit mutation plan and optionally save the asset.
    MCP tool: `unit_apply_plan`. Loads the stored plan, rejects a plan marked inapplicable, and executes its supported mutation type. The result records applied changes and the requested persistence behavior.
    PlanId: Identifier of the reviewed stored unit mutation plan.
    bSaveAssets: Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
    Returns: JSON containing the mutation result, plan_id, saved and applied_diff, or success=false and error for an invalid plan, failed mutation or save failure.
    Before calling: Inspect the diff with UMassBattleUnitMCPApi::MCP_UnitPreviewDiff before submitting a reviewed plan."""
    return await get_connection().send_command(
        "MCP_UnitApplyPlan",
        {"PlanId": plan_id, "bSaveAssets": save_assets},
    )


@mcp.tool()
async def editor_plan_create_vat_unit_from_selection(options: Any = None) -> Dict[str, Any]:
    """Diagnostic: infer the DoAll spec from current editor selection or selected_assets and return it for review.
    MCP tool: `editor_plan_create_vat_unit_from_selection`. Resolves current editor-selected assets, or the explicit selected_assets override, into a VAT creation spec and returns the resulting diagnostic plan. It exposes the resolved inputs for review without generating assets.
    OptionsJson: JSON object; pass {} to use the defaults. selected_assets supplies an explicit selection. Additional generation options feed the selection resolver and returned resolved_spec.
    Returns: JSON with success, selection, resolved_spec, create_plan and ready_to_apply. Empty/unresolvable selections return error information."""
    return await get_connection().send_command(
        "MCP_EditorPlanCreateVatUnitFromSelection",
        {"OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def editor_apply_create_vat_unit_from_selection(
    options: Any = None,
    save_assets: bool = True,
    compact_response: bool = True,
) -> Dict[str, Any]:
    """Primary one-click current selection -> generate entry matching the MassBattleTools DoAll workflow.
    MCP tool: `editor_apply_create_vat_unit_from_selection`. Builds the canonical VAT spec from the current selection or selected_assets and executes the same primary DoAll generation workflow. The MCP tool defaults compact_response to true; explicit options override that value. The selection and resolved inputs remain visible in the response.
    OptionsJson: JSON object; pass {} to use the defaults. selected_assets overrides editor selection; generation options and compact_response are passed to the resolved VAT workflow.
    bSaveAssets: Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
    Returns: JSON with success, selection, resolved_spec and apply_result or apply_summary; failed selection resolution or generation returns the corresponding error."""
    return await get_connection().send_command(
        "MCP_EditorApplyCreateVatUnitFromSelection",
        {"OptionsJson": _json_arg_with_defaults(options, {"compact_response": compact_response}), "bSaveAssets": save_assets},
    )


@mcp.tool()
async def editor_inspect_actor_assembly(actor_path: str, options: Any = None) -> Dict[str, Any]:
    """Inspect an Actor's modular skeletal/static meshes, effective materials, and resolved weapon bind bones without writing assets.
    MCP tool: `editor_inspect_actor_assembly`. Spawns/resolves the requested Actor for inspection, evaluates included skeletal/static components, effective materials and weapon bind bones, and reports whether the assembly can be produced. Bounds are reported in root-skeleton model space without recentering. No generated assets are written.
    ActorPath: Actor Blueprint or class object path. When supplied, it becomes actor_class in the inspection spec.
    OptionsJson: JSON object; pass {} to use the defaults. Optional component_overrides selects assembly participation and component-specific authoring overrides.
    Returns: JSON with success, inspection_type, actor_class, root_component, component counts, components, body_bounds, can_assemble and issues."""
    return await get_connection().send_command(
        "MCP_EditorInspectActorAssembly",
        {"ActorPath": actor_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def editor_inspect_vat_animation(spec: Any) -> Dict[str, Any]:
    """Audit assembled skin weights, VAT position-frame motion, lookup UVs, and the baked soldier animation profile.
    MCP tool: `editor_inspect_vat_animation`. Loads the VAT data and referenced skeletal/static meshes, audits deforming skin weights, position-frame motion and lookup UVs, and compares baked animation paths with the selected soldier animation profile. This entry reports evidence without baking or editing assets.
    SpecJson: JSON containing vat_data_asset (or anim_to_texture_data_asset); skeletal_mesh and static_mesh select the meshes explicitly. ground_tolerance_uu configures the ground audit tolerance in Unreal units.
    Returns: The VAT animation audit JSON, including validity, deforming-weight and motion findings, animation evidence and mesh/UV issues. Invalid JSON or unresolved inputs return success=false and error."""
    return await get_connection().send_command(
        "MCP_EditorInspectVatAnimation",
        {"SpecJson": _json_arg(spec)},
    )


@mcp.tool()
async def editor_plan_create_vat_unit_from_actor(spec: Any) -> Dict[str, Any]:
    """Plan Actor component assembly followed by strict VAT unit authoring without writing assets.
    MCP tool: `editor_plan_create_vat_unit_from_actor`. Inspects the Actor assembly, resolves animation/sample-rate/LOD defaults, validates the required VAT fields and assembly output, then returns a canonical resolved_vat_spec. It reports collisions and assembly issues without creating generated assets.
    SpecJson: JSON with actor_class and optional component_overrides, plus the canonical VAT unit fields. assembled_skeletal_mesh_path chooses the assembly asset explicitly; otherwise target_package_path and unit_name produce SKM_<unit_name>_Assembled, overridable by assembled_skeletal_mesh_name. overwrite_existing controls replacing an assembly output. Required VAT settings include unit_name, target_package_path, parent_material, source_renderer_class, niagara_system, positive vat_sample_rate, animations, and either target_unit or template_unit/target_unit_package_path/subtype.
    Returns: JSON with applicable, assembled_skeletal_mesh, assembly_status, inspection, animation_resolution, vat_sample_rate_resolution, vat_lod_resolution, resolved_vat_spec and issues."""
    return await get_connection().send_command(
        "MCP_EditorPlanCreateVatUnitFromActor",
        {"SpecJson": _json_arg(spec)},
    )


@mcp.tool()
async def editor_apply_create_vat_unit_from_actor(
    spec: Any,
    save_assets: bool = True,
    compact_response: bool = True,
) -> Dict[str, Any]:
    """Assemble an Actor and configured weapon into an animation-compatible SkeletalMesh, then author its strict VAT unit.
    MCP tool: `editor_apply_create_vat_unit_from_actor`. Builds and validates the Actor assembly plan, creates the animation-compatible assembled SkeletalMesh, and passes the resolved canonical spec to the primary VAT generation entry. dry_run/preview_only returns the plan without creating outputs. The MCP tool defaults compact_response to true, overridable in spec.
    SpecJson: JSON with actor_class and optional component_overrides, plus the canonical VAT unit fields. assembled_skeletal_mesh_path chooses the assembly asset explicitly; otherwise target_package_path and unit_name produce SKM_<unit_name>_Assembled, overridable by assembled_skeletal_mesh_name. overwrite_existing controls replacing an assembly output. Includes the canonical VAT inputs documented by the Actor planning entry, plus dry_run/preview_only and compact_response.
    bSaveAssets: Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
    Returns: JSON with the plan or assembly result, skeletal_mesh, resolved_vat_spec and vat_result. Inapplicable plans or failed assembly/VAT steps return their actual errors.
    UMassBattleUnitEditorMCPApi::MCP_EditorPlanCreateVatUnitFromActor for the resolved inputs and assembly preview."""
    return await get_connection().send_command(
        "MCP_EditorApplyCreateVatUnitFromActor",
        {"SpecJson": _json_arg_with_defaults(spec, {"compact_response": compact_response}), "bSaveAssets": save_assets},
    )


@mcp.tool()
async def editor_plan_organize_unit_assets(unit_path: str, options: Any = None) -> Dict[str, Any]:
    """Plan moving a MassBattle unit and linked generated/source assets into the selected style layout.
    MCP tool: `editor_plan_organize_unit_assets`. Resolves the style layout and discovers the unit and linked generated/source assets selected by the organization policy. It calculates source/destination moves and collision status without moving or saving assets.
    UnitPath: Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
    OptionsJson: JSON object; pass {} to use the defaults. style_profile chooses defaults; target_root or target_package_path selects the destination. unit_name, use_family_folder, generated_folder_name/target_folder_name and managed_roots customize the existing layout policy.
    Returns: A reviewable organization plan with moves, source/destination paths, resolved layout and per-move status, or success=false and error."""
    return await get_connection().send_command(
        "MCP_EditorPlanOrganizeUnitAssets",
        {"UnitPath": unit_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def editor_apply_organize_unit_assets(
    unit_path: str,
    options: Any = None,
    save_assets: bool = True,
) -> Dict[str, Any]:
    """Apply a reviewed MassBattle unit organization plan; options default to dry_run=true.
    MCP tool: `editor_apply_organize_unit_assets`. Rebuilds the same organization plan and returns it by default because dry_run=true. With dry_run=false it executes the eligible AssetTools moves and reports each performed move and actual failure.
    UnitPath: Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
    OptionsJson: JSON object; pass {} to use the defaults. Uses the same layout options as the organization planner. dry_run defaults to true; preview_only is also accepted.
    bSaveAssets: Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
    Returns: JSON with dry_run, plan, execution_steps, moved_count and moved; execution failures include success=false/error and the actual move report."""
    return await get_connection().send_command(
        "MCP_EditorApplyOrganizeUnitAssets",
        {"UnitPath": unit_path, "OptionsJson": _json_arg(options), "bSaveAssets": save_assets},
    )


@mcp.tool()
async def effect_asset_get_api_status() -> Dict[str, Any]:
    """List generic MassBattle effect asset API capabilities.
    MCP tool: `effect_asset_get_api_status`. Lists the asset discovery, typed inspection, deterministic text export, additive duplication and renderer-default primitives used to assemble a MassBattle FX authoring workflow.
    
    Returns: JSON with success, api_name, version, model and tools including native parameters."""
    return await get_connection().send_command("MCP_EffectAssetGetApiStatus")


@mcp.tool()
async def effect_asset_query(query: Any) -> Dict[str, Any]:
    """Query Niagara, Cascade, material, Blueprint, and related visual effect assets.
    MCP tool: `effect_asset_query`. Queries supported visual-effect asset classes by path/name text and root. Class filters and recursive_classes control which related asset types are returned for subsequent inspection.
    QueryJson: JSON object with query text, path/root, limit and optional classes array and recursive_classes. Use Unreal class paths for explicit class filters.
    Returns: JSON with success, query, root, count and matching assets. Invalid JSON returns success=false and error."""
    return await get_connection().send_command("MCP_EffectAssetQuery", {"QueryJson": _json_arg(query)})


@mcp.tool()
async def effect_asset_read_summary(asset_path: str, options: Any = None) -> Dict[str, Any]:
    """Read a typed summary for an effect-related asset.
    MCP tool: `effect_asset_read_summary`. Loads the requested visual-effect asset and selects the typed summary appropriate to its actual class. The result supplies asset identity and the class-specific data needed before duplication or conversion.
    AssetPath: Unreal object path of the supported effect-related asset.
    OptionsJson: JSON object; pass {} to use the defaults. Selects the inspection detail supported by the asset summary implementation.
    Returns: The typed summary JSON for the loaded asset, or success=false/error for invalid options, unresolved paths or unsupported inspection input."""
    return await get_connection().send_command(
        "MCP_EffectAssetReadSummary",
        {"AssetPath": asset_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def effect_asset_export_text(asset_path: str, options: Any = None) -> Dict[str, Any]:
    """Export deterministic text for an effect-related asset.
    MCP tool: `effect_asset_export_text`. Loads the asset, includes reflected property/dependency information in its typed summary and produces a deterministic text representation for detailed comparison. write_file controls writing the text artifact in addition to returning it.
    AssetPath: Unreal object path of the effect-related asset to export.
    OptionsJson: JSON object; pass {} to use the defaults. write_file selects whether to write the exported text to disk.
    Returns: JSON containing the asset summary and text, with text_path when written. Invalid JSON or a failed load/export returns success=false and error."""
    return await get_connection().send_command(
        "MCP_EffectAssetExportText",
        {"AssetPath": asset_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def effect_asset_soft_delete(asset_path: str, options: Any = None) -> Dict[str, Any]:
    """Move an unreferenced generic asset to trash; dry_run=true by default.
    MCP tool: `effect_asset_soft_delete`. Builds a trash-move report, checks hard/soft referencers and blocks a referenced asset unless the explicit corresponding override is supplied. The default dry run returns the proposed destination; an executable request uses AssetTools to move the asset and reports the outcome.
    AssetPath: Unreal object path of the existing effect-related asset.
    OptionsJson: JSON object; pass {} to use the defaults. dry_run defaults to true; preview_only also requests a preview. trash_package_path/trash_root select the destination. allow_referenced and allow_unsafe_asset_move/force explicitly opt into the supported exceptional moves.
    Returns: JSON with dry_run, source_path, applicable, blocked_reason and trash_path; execution adds moved and any success/error result."""
    return await get_connection().send_command(
        "MCP_EffectAssetSoftDelete",
        {"AssetPath": asset_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def effect_duplicate_asset(source_asset_path: str, new_asset_name: str, package_path: str, save_assets: bool = True) -> Dict[str, Any]:
    """Duplicate an effect-related asset additively.
    MCP tool: `effect_duplicate_asset`. Uses AssetTools to duplicate the source asset into the requested package and name. The duplicate preserves the source-owned visual data. When left unsaved, it is tracked by this editor session so the matching discard tool can roll it back.
    SourceAssetPath: Unreal object path of the existing source effect asset.
    NewAssetName: Name of the new duplicate asset.
    PackagePath: Destination Unreal package folder, such as /Game/Effects.
    bSaveAssets: Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
    Returns: JSON with success, asset_path, package and saved, or success=false and error if loading, duplication or saving fails."""
    return await get_connection().send_command(
        "MCP_EffectDuplicateAsset",
        {
            "SourceAssetPath": source_asset_path,
            "NewAssetName": new_asset_name,
            "PackagePath": package_path,
            "bSaveAssets": save_assets,
        },
    )


@mcp.tool()
async def batch_fx_read_renderer_defaults(target_class_path: str) -> Dict[str, Any]:
    """Read AMassBattleFxRenderer Blueprint class defaults used by newly placed actors.
    MCP tool: `batch_fx_read_renderer_defaults`. Reads the class-default object of the selected MassBattle FX renderer class. These defaults are the authoring settings inherited by newly placed renderer actors.
    TargetClassPath: Unreal Blueprint/class path resolving to AMassBattleFxRenderer or a derived class.
    Returns: JSON with success and defaults, or success=false/error if the class or default object cannot be resolved."""
    return await get_connection().send_command(
        "MCP_BatchFxReadRendererDefaults",
        {"TargetClassPath": target_class_path},
    )


@mcp.tool()
async def batch_fx_set_renderer_defaults(
    target_class_path: str,
    niagara_system_path: str,
    ndc_burst_fx_path: str,
    subtype: int,
    render_batch_size: int = 2048,
    pooling_cooldown: float = 3.0,
    save_assets: bool = True,
) -> Dict[str, Any]:
    """Set AMassBattleFxRenderer Blueprint CDO defaults for a batched FX subtype.
    MCP tool: `batch_fx_set_renderer_defaults`. Updates the renderer class-default object, recompiles its Blueprint when applicable, marks the package dirty and optionally saves it. Empty system/channel paths keep the existing reference; numeric sentinel values keep the corresponding existing setting.
    TargetClassPath: MassBattle FX renderer Blueprint/class path whose defaults will be edited.
    NiagaraSystemPath: NiagaraSystem object path; an empty string keeps the existing system.
    NdcBurstFxPath: NiagaraDataChannelAsset object path; an empty string keeps the current Burst channel.
    SubType: FX subtype index. A negative value keeps the existing index.
    RenderBatchSize: Positive batch size, or a non-positive value to keep it. The MCP tool default is 2048.
    PoolingCooldown: Pooling cooldown in seconds; a negative value keeps it. The MCP tool default is 3.0.
    bSaveAssets: Whether to save the modified or newly created assets to disk. The MCP tool supplies its documented save_assets default; false leaves changes in editor memory.
    Returns: JSON with success, class_path, niagara_system, ndc_burst_fx, subtype, render_batch_size, pooling_cooldown and saved. Invalid asset references or save errors return success=false/error."""
    return await get_connection().send_command(
        "MCP_BatchFxSetRendererDefaults",
        {
            "TargetClassPath": target_class_path,
            "NiagaraSystemPath": niagara_system_path,
            "NdcBurstFxPath": ndc_burst_fx_path,
            "SubType": subtype,
            "RenderBatchSize": render_batch_size,
            "PoolingCooldown": pooling_cooldown,
            "bSaveAssets": save_assets,
        },
    )


@mcp.tool()
async def niagara_get_api_status() -> Dict[str, Any]:
    """List Niagara MCP API capabilities.
    MCP tool: `niagara_get_api_status`. Returns native Niagara tool capabilities and parameter names for querying, inspecting, comparing and editing existing systems. The entries are graph/property primitives used to construct an explicit authoring workflow.
    
    Returns: JSON with success, api_name, version, model and tools."""
    return await get_connection().send_command("MCP_NiagaraGetApiStatus")


@mcp.tool()
async def niagara_query(query: Any) -> Dict[str, Any]:
    """Query Niagara systems by path or name text.
    MCP tool: `niagara_query`. Scans NiagaraSystem assets under the requested root, filters by path/name text and sorts candidates by object path. matched counts all query matches and returned records the bounded result count.
    QueryJson: JSON object with query text, path/root and limit.
    Returns: JSON with success, matched, returned and systems containing name, object_path and package_path. Invalid JSON returns success=false/error."""
    return await get_connection().send_command("MCP_NiagaraQuery", {"QueryJson": _json_arg(query)})


@mcp.tool()
async def niagara_read_summary(system_path: str, options: Any = None) -> Dict[str, Any]:
    """Read a Niagara system summary.
    MCP tool: `niagara_read_summary`. Loads the Niagara system and summarizes emitters, renderers, user parameters and compile/authoring information. include_modules controls whether function-call module summaries are included.
    SystemPath: Unreal object path of the NiagaraSystem asset.
    OptionsJson: JSON object; pass {} to use the defaults. include_modules selects function-call module summaries.
    Returns: The system summary JSON, or success=false/error for an invalid options object or system path."""
    return await get_connection().send_command(
        "MCP_NiagaraReadSummary",
        {"SystemPath": system_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def niagara_read_module(system_path: str, selector: Any) -> Dict[str, Any]:
    """Read one Niagara module node and pins.
    MCP tool: `niagara_read_module`. Collects the system function-call module nodes and returns the first record matching the provided emitter/module/GUID/index filters. The module record includes its pins for a precise later pin edit.
    SystemPath: Unreal object path of the NiagaraSystem asset.
    SelectorJson: JSON object with optional emitter, module/function_name, node_guid and module_index/index. Use a GUID or collected module index to identify a specific matching node.
    Returns: JSON with success and module. Invalid JSON, an unresolved system or no matching module returns success=false/error."""
    return await get_connection().send_command(
        "MCP_NiagaraReadModule",
        {"SystemPath": system_path, "SelectorJson": _json_arg(selector)},
    )


@mcp.tool()
async def effect_discard_unsaved_duplicate(asset_path: str) -> Dict[str, Any]:
    """Discard only an unsaved in-memory duplicate created by this MCP editor session.
    MCP tool: `effect_discard_unsaved_duplicate`. Discards a duplicate only when this MCP editor session tracked it as unsaved, it is still loaded in a dirty /Game/ package, and it has not crossed the persisted-asset boundary. This supports rolling back a failed conversion before saving.
    AssetPath: Exact object path returned by the duplicate tool for an unsaved duplicate in this editor session.
    Returns: JSON with success, discarded, asset_path, package and deleted_count; an untracked, unloaded or non-dirty duplicate returns success=false/error.
    Before calling: Create the unsaved duplicate with UMassBattleEffectAssetMCPApi::MCP_EffectDuplicateAsset using bSaveAssets=false."""
    return await get_connection().send_command(
        "MCP_EffectDiscardUnsavedDuplicate",
        {"AssetPath": asset_path},
    )


@mcp.tool()
async def niagara_read_graph(system_path: str, selector: Any = None) -> Dict[str, Any]:
    """Read Niagara script graphs with stable node/pin ids and explicit links.
    MCP tool: `niagara_read_graph`. Reads system or standalone NiagaraScript graphs and returns stable node/pin identifiers and explicit links. Optional stack-input topology and compile-state reports provide the identities and diagnostics used by the graph-edit tool.
    SystemPath: Unreal object path of a NiagaraSystem or supported NiagaraScript asset.
    SelectorJson: JSON object; pass {} to use the defaults. include_nodes, include_pins, include_stack_inputs and include_compile_state select returned detail; max_nodes bounds exported graph nodes. Graph selection fields choose the relevant script/emitter scope.
    Returns: JSON containing graph_count, graphs and the resolved system/script identity, plus requested compile_state and stack-input availability. Invalid selectors or unresolved assets return success=false/error."""
    return await get_connection().send_command(
        "MCP_NiagaraReadGraph",
        {"SystemPath": system_path, "SelectorJson": _json_arg(selector)},
    )


@mcp.tool()
async def niagara_compare_systems(
    source_system_path: str,
    target_system_path: str,
    options: Any = None,
) -> Dict[str, Any]:
    """Compare a source Niagara system with a duplicate/translation using source-neutral fingerprints.
    MCP tool: `niagara_compare_systems`. Loads two different systems and compares source-neutral semantic fingerprints. exact mode checks a duplicate; translation mode applies the configured preservation rules for an intended conversion. Compile readiness/error/warning options control the corresponding result checks.
    SourceSystemPath: Unreal object path of the reference NiagaraSystem.
    TargetSystemPath: Unreal object path of a different NiagaraSystem to compare against the source.
    OptionsJson: JSON object; pass {} to use the defaults. mode must be exact or translation. require_ready_to_run, require_no_compile_errors and require_no_warnings select compile-state requirements.
    Returns: The comparison JSON with preservation checks, source/target emitter and renderer counts, unexpected_removed_edges and readiness. Invalid input or identical source/target assets return success=false/error."""
    return await get_connection().send_command(
        "MCP_NiagaraCompareSystems",
        {
            "SourceSystemPath": source_system_path,
            "TargetSystemPath": target_system_path,
            "OptionsJson": _json_arg(options),
        },
    )


@mcp.tool()
async def niagara_read_all(system_path: str, options: Any = None) -> Dict[str, Any]:
    """Read reflected Niagara properties plus every function-call module and pin.
    MCP tool: `niagara_read_all`. Combines the system summary with reflected system/emitter-data properties and every function-call module and pin. This provides the current source-aligned values before forming a property or pin edit.
    SystemPath: Unreal object path of the NiagaraSystem asset.
    OptionsJson: JSON object; pass {} to use the defaults. max_properties bounds reflected properties exported for each inspected object.
    Returns: JSON summary plus system_properties, modules_detailed and emitter_data_properties, or success=false/error for invalid input."""
    return await get_connection().send_command(
        "MCP_NiagaraReadAll",
        {"SystemPath": system_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def niagara_export_text(system_path: str, options: Any = None) -> Dict[str, Any]:
    """Export deterministic Niagara text for close reading.
    MCP tool: `niagara_export_text`. Builds a deterministic text representation of the system summary and detailed modules for close reading and comparison. write_file selects whether the returned text is also written to disk.
    SystemPath: Unreal object path of the existing NiagaraSystem.
    OptionsJson: JSON object; pass {} for defaults. write_file selects file output.
    Returns: JSON with the summary, modules_detailed and text, plus text_path when written. Unresolved systems return success=false/error."""
    return await get_connection().send_command(
        "MCP_NiagaraExportText",
        {"SystemPath": system_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def niagara_merge_write(system_path: str, patch: Any, save_assets: bool = False) -> Dict[str, Any]:
    """Union-merge Niagara property writes. This does not delete.
    MCP tool: `niagara_merge_write`. Resolves explicit system, emitter-data or renderer targets and union-merges requested reflected property values. Omitted properties are retained. Each result records target resolution and before/after values; saving is optional.
    SystemPath: Unreal object path of the existing NiagaraSystem.
    PatchJson: JSON containing patches, or one patch object. Each patch selects a target, property/path and value_text or value. Use real reflected property names from read_all.
    bSaveAssets: Whether to save the modified system. The MCP tool default is save_assets=false.
    Returns: JSON with success, system, saved and per-patch results including target_resolved, before and after. Invalid targets/properties/values or save failures report actual errors."""
    return await get_connection().send_command(
        "MCP_NiagaraMergeWrite",
        {"SystemPath": system_path, "PatchJson": _json_arg(patch), "bSaveAssets": save_assets},
    )


@mcp.tool()
async def niagara_set_module_pin(
    system_path: str,
    selector: Any,
    pin_name: str,
    value_text: str,
    save_assets: bool = True,
) -> Dict[str, Any]:
    """Set one Niagara function-call module input pin default value.
    MCP tool: `niagara_set_module_pin`. Resolves one function-call module and named pin, then writes its default value. Linked pins require allow_linked=true because the default may be ignored; non-input pins require allow_output=true. The report identifies the selected node and old/new text.
    SystemPath: Unreal object path of the existing NiagaraSystem.
    SelectorJson: JSON using emitter, module/function_name, node_guid or module_index; allow_linked and allow_output opt into the corresponding writes.
    PinName: Exact pin name returned by module inspection.
    ValueText: Default value in the selected pin type text/import syntax.
    bSaveAssets: Whether to save the modified system. The MCP tool default is save_assets=true.
    Returns: JSON with system, module_index, emitter, module, pin, linked_to_count, before, after and saved. Unresolved or ambiguous targets and refused pins return success=false/error."""
    return await get_connection().send_command(
        "MCP_NiagaraSetModulePin",
        {
            "SystemPath": system_path,
            "SelectorJson": _json_arg(selector),
            "PinName": pin_name,
            "ValueText": value_text,
            "bSaveAssets": save_assets,
        },
    )


@mcp.tool()
async def niagara_apply_graph_edit(
    system_path: str,
    edit: Any,
    save_assets: bool = False,
) -> Dict[str, Any]:
    """Apply ordered Niagara graph edits, compile once, validate preservation, and optionally save.
    MCP tool: `niagara_apply_graph_edit`. Validates ordered operation records and ids, executes explicit graph/stack edits, compiles once and checks source preservation before saving. Operations include emitter copying, user values/data interfaces, module/assignment insertion, pin connection/disconnection/rewiring and stack-input edits. Empty operations requests compile/validation only.
    SystemPath: Unreal object path of the existing NiagaraSystem.
    EditJson: JSON with an operations array (alias edits). Each object supplies op/type, optional unique id and the fields required for its operation and source-preservation policy.
    bSaveAssets: Whether to save the modified system. The MCP tool default is save_assets=false.
    Returns: The ordered operation, compile and preservation-validation report with actual edit results and saved state. Malformed operations, failed edits or failed validation report their errors."""
    return await get_connection().send_command(
        "MCP_NiagaraApplyGraphEdit",
        {"SystemPath": system_path, "EditJson": _json_arg(edit), "bSaveAssets": save_assets},
    )


@mcp.tool()
async def niagara_batch_translate(
    manifest: Any,
    apply: bool = False,
    save_assets: bool = True,
) -> Dict[str, Any]:
    """Preflight or execute an explicit source-first Niagara translation manifest; never guesses visual edits."""
    if isinstance(manifest, str):
        try:
            manifest = json.loads(manifest)
        except Exception as exc:
            return {"success": False, "error": f"Manifest is not valid JSON: {exc}"}
    if not isinstance(manifest, dict):
        return {"success": False, "error": "Manifest must be a JSON object."}

    schema = manifest.get("schema", "massbattle.niagara.translation_manifest.v1")
    if schema != "massbattle.niagara.translation_manifest.v1":
        return {"success": False, "error": f"Unsupported manifest schema: {schema}"}
    items = manifest.get("items")
    if not isinstance(items, list) or not items:
        return {"success": False, "error": "Manifest requires a non-empty items array."}
    if len(items) > 256:
        return {"success": False, "error": "Manifest items is limited to 256 entries."}

    stop_on_error = bool(manifest.get("stop_on_error", True))
    preflight: list[Dict[str, Any]] = []
    preflight_errors: list[Dict[str, Any]] = []
    seen_ids: set[str] = set()
    seen_targets: set[str] = set()

    for index, raw_item in enumerate(items):
        item_errors: list[str] = []
        if not isinstance(raw_item, dict):
            preflight_errors.append({"index": index, "errors": ["item must be an object"]})
            continue

        item_id = str(raw_item.get("id") or f"item_{index:03d}")
        source = raw_item.get("source_system_path")
        new_name = raw_item.get("new_asset_name")
        package_path = raw_item.get("package_path")
        edit = raw_item.get("edit", {"operations": []})
        comparison = raw_item.get("comparison", {})
        if isinstance(edit, str):
            try:
                edit = json.loads(edit)
            except Exception as exc:
                item_errors.append(f"edit is not valid JSON: {exc}")
        if isinstance(comparison, str):
            try:
                comparison = json.loads(comparison)
            except Exception as exc:
                item_errors.append(f"comparison is not valid JSON: {exc}")

        if item_id in seen_ids:
            item_errors.append(f"duplicate id: {item_id}")
        seen_ids.add(item_id)
        if not isinstance(source, str) or not source.strip():
            item_errors.append("source_system_path is required")
        if not isinstance(new_name, str) or not new_name.strip():
            item_errors.append("new_asset_name is required")
        if not isinstance(package_path, str) or not package_path.startswith("/Game/"):
            item_errors.append("package_path must start with /Game/")
        if not isinstance(edit, dict) or not isinstance(edit.get("operations"), list):
            item_errors.append("edit must be an object with an operations array")
        if not isinstance(comparison, dict):
            item_errors.append("comparison must be an object")

        target = ""
        if isinstance(new_name, str) and isinstance(package_path, str):
            package_path = package_path.rstrip("/")
            target = f"{package_path}/{new_name}.{new_name}"
            if target in seen_targets:
                item_errors.append(f"duplicate target: {target}")
            seen_targets.add(target)
            if source == target:
                item_errors.append("source and target must be different assets")

        source_read: Dict[str, Any] = {}
        target_read: Dict[str, Any] = {}
        if not item_errors:
            source_read = await get_connection().send_command(
                "MCP_NiagaraReadSummary",
                {"SystemPath": source, "OptionsJson": '{"include_modules":false}'},
            )
            if not source_read.get("success"):
                item_errors.append(f"source read failed: {source_read.get('error', source_read)}")
            else:
                target_read = await get_connection().send_command(
                    "MCP_NiagaraReadSummary",
                    {"SystemPath": target, "OptionsJson": '{"include_modules":false}'},
                )
                if target_read.get("success"):
                    item_errors.append(f"target already exists: {target}")

        record = {
            "index": index,
            "id": item_id,
            "source": source,
            "target": target,
            "edit": edit,
            "comparison": comparison,
            "source_ready_to_run": source_read.get("ready_to_run"),
            "errors": item_errors,
        }
        preflight.append(record)
        if item_errors:
            preflight_errors.append({"index": index, "id": item_id, "errors": item_errors})

    if preflight_errors or not apply:
        return {
            "success": not preflight_errors,
            "schema": schema,
            "apply_requested": apply,
            "mutated": False,
            "item_count": len(items),
            "preflight": preflight,
            "errors": preflight_errors,
        }

    results: list[Dict[str, Any]] = []

    async def discard_failed_duplicate(result: Dict[str, Any], target: str) -> None:
        cleanup = await get_connection().send_command(
            "MCP_EffectDiscardUnsavedDuplicate",
            {"AssetPath": target},
        )
        result["cleanup"] = cleanup

    for item in preflight:
        result: Dict[str, Any] = {
            "index": item["index"],
            "id": item["id"],
            "source": item["source"],
            "target": item["target"],
            "success": False,
            "saved": False,
        }
        duplicate = await get_connection().send_command(
            "MCP_EffectDuplicateAsset",
            {
                "SourceAssetPath": item["source"],
                "NewAssetName": item["target"].rsplit("/", 1)[-1].split(".", 1)[0],
                "PackagePath": item["target"].rsplit("/", 1)[0],
                "bSaveAssets": False,
            },
        )
        result["duplicate"] = duplicate
        if not duplicate.get("success"):
            result["error"] = f"duplicate failed: {duplicate.get('error', duplicate)}"
            results.append(result)
            if stop_on_error:
                break
            continue

        target = duplicate.get("asset_path", item["target"])
        clone_compare = await get_connection().send_command(
            "MCP_NiagaraCompareSystems",
            {
                "SourceSystemPath": item["source"],
                "TargetSystemPath": target,
                # Asset duplication is intentionally unsaved and may not have entered Niagara's
                # compile queue yet.  This gate proves source-neutral structural identity; the
                # following graph-edit barrier compiles and enforces ready-to-run before save.
                "OptionsJson": '{"mode":"exact","require_ready_to_run":false}',
            },
        )
        result["clone_comparison"] = clone_compare
        if not clone_compare.get("success"):
            result["error"] = "duplicate is not an exact source-neutral clone"
            await discard_failed_duplicate(result, target)
            results.append(result)
            if stop_on_error:
                break
            continue

        edit_result = await get_connection().send_command(
            "MCP_NiagaraApplyGraphEdit",
            {
                "SystemPath": target,
                "EditJson": _json_arg(item["edit"]),
                "bSaveAssets": False,
            },
        )
        result["edit"] = edit_result
        if not edit_result.get("success"):
            result["error"] = f"graph edit failed: {edit_result.get('error', edit_result)}"
            await discard_failed_duplicate(result, target)
            results.append(result)
            if stop_on_error:
                break
            continue

        comparison = dict(item["comparison"])
        comparison.setdefault("mode", "translation")
        comparison.setdefault("require_ready_to_run", True)
        translation_compare = await get_connection().send_command(
            "MCP_NiagaraCompareSystems",
            {
                "SourceSystemPath": item["source"],
                "TargetSystemPath": target,
                "OptionsJson": _json_arg(comparison),
            },
        )
        result["translation_comparison"] = translation_compare
        if not translation_compare.get("success"):
            result["error"] = "translation did not satisfy the declared source-preservation policy"
            await discard_failed_duplicate(result, target)
            results.append(result)
            if stop_on_error:
                break
            continue

        if save_assets:
            save_result = await get_connection().send_command(
                "MCP_NiagaraApplyGraphEdit",
                {
                    "SystemPath": target,
                    "EditJson": '{"operations":[],"validation":{"require_ready_to_run":true}}',
                    "bSaveAssets": True,
                },
            )
            result["save"] = save_result
            if not save_result.get("success") or not save_result.get("saved"):
                result["error"] = f"save validation failed: {save_result.get('error', save_result)}"
                await discard_failed_duplicate(result, target)
                results.append(result)
                if stop_on_error:
                    break
                continue
            result["saved"] = True

        result["success"] = True
        results.append(result)

    failed = [item for item in results if not item.get("success")]
    return {
        "success": not failed and len(results) == len(items),
        "schema": schema,
        "apply_requested": True,
        "mutated": bool(results),
        "save_requested": save_assets,
        "item_count": len(items),
        "processed_count": len(results),
        "failed_count": len(failed),
        "results": results,
    }


@mcp.tool()
async def niagara_set_emitter_enabled(system_path: str, emitter_name: str, enabled: bool, save_assets: bool = True) -> Dict[str, Any]:
    """Explicitly enable or disable one Niagara emitter handle.
    MCP tool: `niagara_set_emitter_enabled`. Finds the named emitter handle, sets its enabled state explicitly, records whether it changed and optionally saves the system.
    SystemPath: Unreal object path of the existing NiagaraSystem.
    EmitterName: Name of the existing emitter handle.
    bEnabled: Desired enabled state of the emitter.
    bSaveAssets: Whether to save the modified system. The MCP tool default is save_assets=true.
    Returns: JSON with success, system, emitter, before, after, changed and saved. Unresolved emitters or save failures return success=false/error."""
    return await get_connection().send_command(
        "MCP_NiagaraSetEmitterEnabled",
        {
            "SystemPath": system_path,
            "EmitterName": emitter_name,
            "bEnabled": enabled,
            "bSaveAssets": save_assets,
        },
    )


@mcp.tool()
async def niagara_delete(system_path: str, delete_spec: Any, save_assets: bool = False) -> Dict[str, Any]:
    """Run explicit Niagara delete operations such as renderer or user-parameter removal.
    MCP tool: `niagara_delete`. Executes explicit renderer removal, user-parameter removal or emitter disabling on the selected system. Each action identifies its target and reports the actual removed/changed result.
    SystemPath: Unreal object path of the existing NiagaraSystem.
    DeleteJson: JSON selecting type=renderer, user_parameter or disable_emitter. Renderer removal selects emitter and renderer_index/index; user_parameter removal requires name; disabling names an emitter.
    bSaveAssets: Whether to save the modified system. The MCP tool default is save_assets=false.
    Returns: JSON with success, system, operation, target identifiers, removed/changed and saved, or success=false/error for invalid operations or targets."""
    return await get_connection().send_command(
        "MCP_NiagaraDelete",
        {"SystemPath": system_path, "DeleteJson": _json_arg(delete_spec), "bSaveAssets": save_assets},
    )


@mcp.tool()
async def niagara_add_sprite_renderer(
    system_path: str,
    emitter_name: str,
    renderer: Any,
    save_assets: bool = False,
) -> Dict[str, Any]:
    """Add one configured sprite renderer to an existing Niagara emitter.
    MCP tool: `niagara_add_sprite_renderer`. Allocates and configures a sprite renderer on an existing emitter, including its material, user-parameter bindings, sort order and visibility. It adds the renderer without rewriting the emitter graph, then finishes compilation and optional saving.
    SystemPath: Unreal object path of the existing NiagaraSystem.
    EmitterName: Name of the existing emitter receiving the new renderer.
    RendererJson: JSON with material and optional material_user_parameter, enabled_user_parameter, sort_order and renderer_visibility.
    bSaveAssets: Whether to save the modified system. The MCP tool default is save_assets=false.
    Returns: The renderer-add/compile report with the created renderer and saved state. Invalid JSON, unresolved emitters/materials or allocation failures return success=false/error."""
    return await get_connection().send_command(
        "MCP_NiagaraAddSpriteRenderer",
        {
            "SystemPath": system_path,
            "EmitterName": emitter_name,
            "RendererJson": _json_arg(renderer),
            "bSaveAssets": save_assets,
        },
    )


if __name__ == "__main__":
    try:
        logger.info("Starting MassBattleEditorMCP server on stdio; Unreal bridge target %s:%s", UNREAL_HOST, UNREAL_PORT)
        mcp.run(transport="stdio")
    except Exception as exc:
        logger.exception("MassBattleEditorMCP server failed to start: %s", exc)
        print("MASSBATTLE_MCP_SERVER_START_FAILED")
