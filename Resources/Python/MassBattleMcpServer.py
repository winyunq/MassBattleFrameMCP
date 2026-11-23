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
    """Discover the unit operations available in the connected editor.
    Returns the native unit API identity and categorized tools with their descriptions. This is capability discovery and does not load or modify a unit asset.
    Returns: The unit API's identity, implementation module, dependency notes and tools grouped by capability."""
    return await get_connection().send_command("MCP_UnitGetApiStatus")


@mcp.tool()
async def unit_list(options: Any = None) -> Dict[str, Any]:
    """Find unit assets and their balance values by path.
    Scans unit config assets, filters their object paths by query, and returns compact balance summaries. load_fields controls whether assets are loaded for authored values. The returned total_scanned counts candidates before the query and result limit.
    OptionsJson: JSON object; pass {} to use the defaults. query is case-insensitive path text; limit is clamped to 1..2000; load_fields defaults to true.
    Returns: Unit summaries and search metadata; count is the returned item count, while total_scanned counts candidates before filtering and limiting."""
    return await get_connection().send_command("MCP_UnitList", {"OptionsJson": _json_arg(options)})


@mcp.tool()
async def unit_get(unit_path: str, options: Any = None) -> Dict[str, Any]:
    """Inspect authored unit data and source Actor export information.
    Reads the Unit Source Actor's UnitData or the unit DataAsset selected by UnitPath. Read the source to review authored settings and the exported unit to verify the result. Source responses include export_path, exported_unit and last_report. The default simple view omits runtime, system and deprecated fields.
    UnitPath: Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
    OptionsJson: JSON object; pass {} to use the defaults. Select the requested detail/object view and field inclusion policy; the response reports detail and default_ignore_policy.
    Returns: The unit's identity and authored configuration in Data, with detail identifying the selected field view."""
    return await get_connection().send_command(
        "MCP_UnitGet",
        {"UnitPath": unit_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def unit_get_schema(options: Any = None) -> Dict[str, Any]:
    """Identify field names, types and nested structures accepted by unit patches.
    Builds the editable property schema of UMassBattleAgentConfigDataAsset and applies the same default field-ignore policy as unit reads. Recursive mode expands nested struct fields so a caller can build a patch from real property names.
    OptionsJson: JSON object; pass {} to use the defaults. recursive defaults to true; depth defaults to 3 in recursive mode and 0 otherwise, and is clamped to 0..8.
    Returns: The supported unit class and editable field definitions, including their types, nested structure and default field filtering policy."""
    return await get_connection().send_command("MCP_UnitGetSchema", {"OptionsJson": _json_arg(options)})


@mcp.tool()
async def unit_export(options: Any = None) -> Dict[str, Any]:
    """Produce JSON or CSV balance data for comparison and tuning.
    Reads the selected unit balance fields and writes a JSON or CSV table. With no output_path, files are placed under the project Saved/MassBattleEditorMCP/Exports directory. The response identifies the created file and exported unit count.
    OptionsJson: JSON object; pass {} to use the defaults. format chooses JSON or CSV; output_path selects the destination file. Unit scan and field options choose table contents.
    Returns: The exported file's output_path, format and unit_count; failures identify invalid options or a file that could not be saved."""
    return await get_connection().send_command("MCP_UnitExport", {"OptionsJson": _json_arg(options)})


@mcp.tool()
async def unit_create(options: Any, save_assets: bool = True) -> Dict[str, Any]:
    """Create a unit DataAsset or a Unit Source Actor.
    Fills omitted creation values from the selected style, preserving explicitly supplied values. asset_type=source_actor creates a Unit Source Actor Blueprint. The destination must be unoccupied.
    OptionsJson: JSON with unit_path or both asset_name and package_path. style_profile is optional and defaults to default. template_unit selects a template and unit_data supplies initial authored values. asset_type=source_actor selects the source Blueprint branch and export_path configures its output.
    bSaveAssets: Whether to save the assets modified or created by this operation to disk.
    Returns: The created asset's identity and persistence result; failures explain destination conflicts or initial-data merge problems."""
    return await get_connection().send_command(
        "MCP_UnitCreate",
        {"OptionsJson": _json_arg(options), "bSaveAssets": save_assets},
    )


@mcp.tool()
async def unit_delete(unit_path: str, options: Any = None) -> Dict[str, Any]:
    """Check or execute a unit asset deletion.
    dry_run=true checks the selected asset and reports whether deletion is allowed. dry_run=false performs the deletion if its reference checks pass.
    UnitPath: Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
    OptionsJson: JSON object; pass {} to use the defaults. dry_run defaults to true. mode selects a soft move to trash or permanent deletion.
    Returns: Deletion eligibility or the executed deletion result."""
    return await get_connection().send_command(
        "MCP_UnitDelete",
        {"UnitPath": unit_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def unit_find_assets(query: Any) -> Dict[str, Any]:
    """Locate meshes, renderers and effects for unit authoring.
    Searches meshes, renderers, Niagara systems, AnimToTexture assets and existing unit configs, then sorts matches by object path for stable authoring discovery.
    QueryJson: JSON query object containing query text and limit. Results identify asset name, path, class, package_path and inferred style_family.
    Returns: Matching asset names, object paths, classes, package folders and inferred style families, with the returned count."""
    return await get_connection().send_command("MCP_UnitFindAssets", {"QueryJson": _json_arg(query)})


@mcp.tool()
async def projectile_get_api_status() -> Dict[str, Any]:
    """Discover projectile editing and validation operations in the editor.
    Returns projectile DataAsset capabilities, native parameter names and write-contract metadata. It identifies the schema, CRUD and validation entries available to the connected editor.
    Returns: The projectile API version, supported asset type, write contract and available tools."""
    return await get_connection().send_command("MCP_ProjectileGetApiStatus")


@mcp.tool()
async def projectile_list(options: Any = None) -> Dict[str, Any]:
    """Filter projectile assets by path and inspect their configuration summaries.
    Scans projectile configuration DataAssets, filters object paths by query and returns stable asset summaries. load_fields selects whether authored fields are loaded for each result.
    OptionsJson: JSON object; pass {} to use the defaults. query filters paths; limit bounds the result count; load_fields controls summary detail. Scan roots select the content scope.
    Returns: Matching projectile summaries and searched roots; count is the returned item count and total_scanned is the number scanned."""
    return await get_connection().send_command(
        "MCP_ProjectileList",
        {"OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def projectile_get(projectile_path: str, options: Any = None) -> Dict[str, Any]:
    """Inspect active projectile data with optional validation and references.
    Loads one projectile DataAsset and returns source-aligned authored data. The active view selects fields relevant to the configured movement and damage modes; the full view can include inactive fields. Validation and referencing assets can be included in the same read.
    ProjectilePath: Unreal object path of the projectile configuration DataAsset.
    OptionsJson: JSON object; pass {} to use the defaults. view/detail and include_inactive select the field view; include_validation and include_referencers request the corresponding reports.
    Returns: The authored configuration in data and its movement and damage modes; view identifies the field view, with validation and referencers included when requested."""
    return await get_connection().send_command(
        "MCP_ProjectileGet",
        {"ProjectilePath": projectile_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def projectile_get_schema(options: Any = None) -> Dict[str, Any]:
    """Obtain field types and write rules for movement and damage modes.
    Reflects projectile field names, types, enums, edit conditions and tooltips. It returns movement/damage-mode field groups and the source-aligned write, damage-ownership and lifecycle rules needed to form a valid patch. Array writes merge by index and reject appending by default; options.append_arrays=true permits append and options.array_mode=replace requests replacement. The returned ownership rule assigns normal projectile attack damage to the projectile DataAsset with Agent Attack.TimeOfHitAction=None; the lifecycle rule identifies duplicate full explosions in OnHit and OnRemoval.
    OptionsJson: JSON object; pass {} to use the defaults. depth selects the nested schema expansion depth.
    Returns: Field definitions and mode-specific field groups, together with array-write, damage-ownership and lifecycle rules."""
    return await get_connection().send_command(
        "MCP_ProjectileGetSchema",
        {"OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def projectile_create(create_spec: Any, save_assets: bool = True) -> Dict[str, Any]:
    """Create a validated projectile config from a template or class defaults.
    Builds a transient preview from the requested template or class defaults, applies initial source-aligned data and validates it before creating the real projectile DataAsset. It rejects an invalid or occupied destination instead of replacing that asset.
    CreateSpecJson: JSON with asset_name (alias name), package_path (alias target_package_path) and optional template_path/template plus initial projectile data. package_path must be an Unreal package path beginning with /.
    bSaveAssets: Whether to save the assets modified or created by this operation to disk.
    Returns: The new asset and template paths, resulting configuration and changed field paths; saved indicates whether the asset was written to disk."""
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
    """Validate a partial update before changing the projectile asset.
    Applies the partial source-aligned patch to a transient copy and validates the candidate before mutating the real projectile configuration. Omitted fields are retained; use the schema write rules when changing arrays or nested authored structs.
    ProjectilePath: Unreal object path of the existing projectile DataAsset.
    PatchJson: Nonempty partial projectile JSON using real source field names. Read the projectile schema for enum values, conditions and array write rules. options.append_arrays and options.array_mode explicitly select array append/replacement behavior.
    bSaveAssets: Whether to save the assets modified or created by this operation to disk.
    Returns: The actual changed field paths, resulting configuration and validation report; saved identifies persistence, and failures report field, preflight or save errors.
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
    """Find movement, damage ownership and lifecycle effect configuration issues.
    Checks movement configuration, damage ownership, trigger/lifetime behavior, lifecycle effects and duplicate explosion risks on the requested projectile. It does not rewrite the configuration.
    ProjectilePath: Unreal object path of the projectile DataAsset to inspect.
    OptionsJson: JSON object; pass {} to use the defaults. include_referencers includes assets that reference the projectile.
    Returns: The projectile validation conclusion and findings in validation, with referencing assets included when requested."""
    return await get_connection().send_command(
        "MCP_ProjectileValidate",
        {"ProjectilePath": projectile_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def projectile_delete(projectile_path: str, options: Any = None) -> Dict[str, Any]:
    """Check references before previewing or executing asset removal.
    Builds a deletion/trash move report and checks references. dry_run=true returns the report without changing the DataAsset; an explicit executable request performs the selected supported mode and returns the actual move/delete outcome.
    ProjectilePath: Unreal object path of the projectile configuration to remove.
    OptionsJson: JSON object; pass {} to use the defaults. dry_run defaults to true. mode chooses the supported deletion path; trash_root selects trash placement. Referenced/unsafe/hard operations require their corresponding allow_referenced, allow_unsafe_asset_move or allow_hard_delete option.
    Returns: The removal report with referencers, applicability and blocking reasons; execution adds the actual trash destination or deletion count."""
    return await get_connection().send_command(
        "MCP_ProjectileDelete",
        {"ProjectilePath": projectile_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def editor_get_status() -> Dict[str, Any]:
    """Discover VAT authoring and asset organization capabilities in the editor.
    Lists available styles, VAT generation, Actor assembly and asset organization capabilities.
    Returns: The editor API identity, capability description and tools grouped by authoring workflow."""
    return await get_connection().send_command("MCP_EditorGetStatus")


@mcp.tool()
async def editor_list_styles(options: Any = None) -> Dict[str, Any]:
    """List the available unit styles.
    Lists the styles that provide creation defaults. New unit creation uses the default style when the caller omits style_profile.
    OptionsJson: JSON object. Pass {} to list the installed styles.
    Returns: Installed styles and their identifiers."""
    return await get_connection().send_command("MCP_EditorListStyles", {"OptionsJson": _json_arg(options)})


@mcp.tool()
async def editor_get_style(style_id: str) -> Dict[str, Any]:
    """Read a unit style and its creation defaults.
    Loads the style file identified by StyleId.
    StyleId: Style identifier returned by the style-list tool.
    Returns: The style contents and source file path, or an error for an unavailable or malformed style.
    UMassBattleUnitEditorMCPApi::MCP_EditorListStyles to discover style identifiers."""
    return await get_connection().send_command(
        "MCP_EditorGetStyle",
        {"StyleId": style_id},
    )


@mcp.tool()
async def editor_plan_create_vat_unit(options: Any) -> Dict[str, Any]:
    """Inspect source assets and output layout before VAT generation.
    For a new unit, resolve omitted generation options from the selected style, then plan the generated asset references and explicit unit patch. An explicit target_unit selects an existing unit for a requested VAT rebuild. The unit patch contains the requested edits and generated references for that rebuild.
    OptionsJson: JSON object with source skeletal_mesh, unit_name, output location, optional style_profile for creation, and unit_patch. Use target_unit for an explicit rebuild of an existing unit.
    Returns: The resolved output layout, discovered inputs and proposed generation steps, with applicability and warnings describing unresolved problems.
    UMassBattleUnitEditorMCPApi::MCP_EditorApplyCreateVatUnit for the primary generation entry."""
    return await get_connection().send_command("MCP_EditorPlanCreateVatUnit", {"OptionsJson": _json_arg(options)})


@mcp.tool()
async def editor_validate_create_vat_unit(options: Any) -> Dict[str, Any]:
    """Check required VAT fields and referenced assets.
    Resolves the skeletal mesh, parent material, renderer and Niagara asset references, checks the animation category mapping and VAT settings, and combines the findings with the planned execution steps.
    OptionsJson: JSON options accepted by UMassBattleUnitEditorMCPApi::MCP_EditorApplyCreateVatUnit. The validation checks mesh, animation, material, renderer, output and subtype settings before generating assets.
    Returns: The validation conclusion in valid, field problems in issues and proposed workflow in execution_preview."""
    return await get_connection().send_command("MCP_EditorValidateCreateVatUnit", {"OptionsJson": _json_arg(options)})


@mcp.tool()
async def editor_apply_create_vat_unit(
    options: Any,
    save_assets: bool = True,
    compact_response: bool = True,
) -> Dict[str, Any]:
    """Create a unit that plays baked skeletal animation through a static mesh.
    Converts the skeletal mesh, bakes animation textures and configures the renderer. New units use the selected style for omitted creation values. A rebuild of an existing unit writes the generated references and explicitly requested unit changes.
    OptionsJson: JSON options for skeletal_mesh, unit_name, output, parent_material, source_renderer_class, niagara_system, vat_sample_rate and animations. Supply target_unit to rebuild an existing unit. style_profile is optional when creating a unit. dry_run or preview_only selects a preview, overwrite_existing allows replacement of generated outputs, and compact_response selects result detail.
    bSaveAssets: Whether to save the assets modified or created by this operation to disk.
    Returns: Generation-step results or a compact summary; previews return the plan and validation. Validation failures are described by validation.valid and validation.issues; execution_steps records completed and failed steps when execution stops.
    Validation is performed automatically; diagnostic plan/validate calls are optional before this primary entry."""
    return await get_connection().send_command(
        "MCP_EditorApplyCreateVatUnit",
        {"OptionsJson": _json_arg_with_defaults(options, {"compact_response": compact_response}), "bSaveAssets": save_assets},
    )


@mcp.tool()
async def unit_write(unit_path: str, unit_data: Any, save_assets: bool = True) -> Dict[str, Any]:
    """Write the requested fields of an existing unit.
    Target the Actor Source Blueprint for routine edits or a unit DataAsset for direct edits to that asset. Fields omitted from the patch keep their current values.
    UnitPath: Unreal object path of the Actor Source Blueprint or unit DataAsset.
    UnitDataJson: Partial unit JSON using source property names. Use options.export_source=true to regenerate the linked unit when saving the Actor Source.
    bSaveAssets: Save the edited asset to disk. When false, edit it only in memory.
    Returns: Success or an error explaining which write failed."""
    return await get_connection().send_command(
        "MCP_UnitWrite",
        {"UnitPath": unit_path, "UnitDataJson": _json_arg(unit_data), "bSaveAssets": save_assets},
    )


@mcp.tool()
async def editor_plan_create_vat_unit_from_selection(options: Any = None) -> Dict[str, Any]:
    """Inspect VAT creation options from selected assets.
    Review the selected assets and generation plan, then pass resolved_options to UMassBattleUnitEditorMCPApi::MCP_EditorApplyCreateVatUnit.
    OptionsJson: JSON object. selected_assets supplies asset paths for selection resolution. Additional generation options are included in resolved_options.
    Returns: The resolved selection, generation options and plan. ready_to_apply indicates whether the request is ready for execution."""
    return await get_connection().send_command(
        "MCP_EditorPlanCreateVatUnitFromSelection",
        {"OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def editor_inspect_actor_assembly(actor_path: str, options: Any = None) -> Dict[str, Any]:
    """Check component materials and weapon bones before assembling an Actor.
    Spawns/resolves the requested Actor for inspection, evaluates included skeletal/static components, effective materials and weapon bind bones, and reports whether the assembly can be produced. Bounds are reported in root-skeleton model space without recentering. No generated assets are written.
    ActorPath: Actor Blueprint or class object path. When supplied, it sets actor_class for the inspection.
    OptionsJson: JSON object; pass {} to use the defaults. Optional component_overrides selects assembly participation and component-specific authoring overrides.
    Returns: An assembly inspection containing components, materials, weapon binding information, bounds and issues; can_assemble indicates assembly feasibility."""
    return await get_connection().send_command(
        "MCP_EditorInspectActorAssembly",
        {"ActorPath": actor_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def editor_inspect_vat_animation(options: Any) -> Dict[str, Any]:
    """Check skin weights, frame motion, lookup UVs and animation settings.
    Loads the VAT data and referenced meshes, audits deforming skin weights, position-frame motion and lookup UVs, and reports the baked animations without editing assets.
    OptionsJson: JSON containing vat_data_asset or anim_to_texture_data_asset. skeletal_mesh and static_mesh select the meshes explicitly. ground_tolerance_uu sets the ground audit tolerance in Unreal units.
    Returns: The VAT animation audit, including skin-weight, frame-motion, animation-resource and UV evidence together with detected issues."""
    return await get_connection().send_command(
        "MCP_EditorInspectVatAnimation",
        {"OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def editor_plan_create_vat_unit_from_actor(options: Any) -> Dict[str, Any]:
    """Inspect the Actor assembly plan and VAT creation options.
    Inspects the Actor's meshes, materials and bindings. For a new unit the style fills omitted generation options. A rebuild uses the supplied options and an explicit target.
    OptionsJson: JSON object with actor_class, optional component_overrides and the options documented by UMassBattleUnitEditorMCPApi::MCP_EditorApplyCreateVatUnit.
    Returns: The resolved_options and proposed assembled mesh. applicable and issues report whether the plan can run."""
    return await get_connection().send_command(
        "MCP_EditorPlanCreateVatUnitFromActor",
        {"OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def editor_apply_create_vat_unit_from_actor(
    options: Any,
    save_assets: bool = True,
    compact_response: bool = True,
) -> Dict[str, Any]:
    """Combine an Actor's components and weapon into an animated VAT unit.
    Assembles the Actor components into an animation-compatible skeletal mesh, then passes that mesh and the resolved options to UMassBattleUnitEditorMCPApi::MCP_EditorApplyCreateVatUnit.
    OptionsJson: JSON object with actor_class, optional component_overrides and VAT generation options. assembled_skeletal_mesh_path or assembled_skeletal_mesh_name selects the assembly output. overwrite_existing selects replacement and dry_run or preview_only selects preview. compact_response controls result detail.
    bSaveAssets: Whether to save the assets modified or created by this operation to disk.
    Returns: The plan for a preview request, or the assembled skeletal mesh, resolved_options and vat_result. Failures report the relevant assembly or VAT step.
    UMassBattleUnitEditorMCPApi::MCP_EditorPlanCreateVatUnitFromActor for the resolved inputs and assembly preview."""
    return await get_connection().send_command(
        "MCP_EditorApplyCreateVatUnitFromActor",
        {"OptionsJson": _json_arg_with_defaults(options, {"compact_response": compact_response}), "bSaveAssets": save_assets},
    )


@mcp.tool()
async def editor_apply_organize_unit_assets(
    unit_path: str,
    options: Any = None,
    save_assets: bool = True,
) -> Dict[str, Any]:
    """Preview or move a unit and its linked source and generated assets into a style layout.
    With dry_run=true it returns the proposed moves and collision checks in plan. With dry_run=false it executes the eligible AssetTools moves and reports each performed move and actual failure.
    UnitPath: Unreal object path of the existing MassBattle unit config or supported Unit Source Actor Blueprint.
    OptionsJson: JSON object. dry_run defaults to true, and preview_only takes precedence when supplied. style_profile chooses defaults, and target_root or target_package_path selects the destination. unit_name, use_family_folder, generated_folder_name/target_folder_name and managed_roots customize the layout.
    bSaveAssets: Whether to save the assets modified or created by this operation to disk.
    Returns: With dry_run=true, plan contains the proposed moves and the resolved style layout. After execution, the response includes per-step results and the actual moved count."""
    return await get_connection().send_command(
        "MCP_EditorApplyOrganizeUnitAssets",
        {"UnitPath": unit_path, "OptionsJson": _json_arg(options), "bSaveAssets": save_assets},
    )


@mcp.tool()
async def effect_asset_get_api_status() -> Dict[str, Any]:
    """Discover effect asset operations available in the connected editor.
    Lists the asset discovery, typed inspection, deterministic text export, additive duplication and renderer-default primitives used to assemble a MassBattle FX authoring workflow.
    Returns: The effect asset API version and operating model together with supported tools and native parameter definitions."""
    return await get_connection().send_command("MCP_EffectAssetGetApiStatus")


@mcp.tool()
async def effect_asset_query(query: Any) -> Dict[str, Any]:
    """Locate supported effect assets by folder, name and class.
    Queries supported visual-effect asset classes by path/name text and root. Class filters and recursive_classes control which related asset types are returned for subsequent inspection.
    QueryJson: JSON object with query text, path/root, limit and optional classes array and recursive_classes. Use Unreal class paths for explicit class filters.
    Returns: Matching effect assets and their count, with the effective search root and query."""
    return await get_connection().send_command("MCP_EffectAssetQuery", {"QueryJson": _json_arg(query)})


@mcp.tool()
async def effect_asset_read_summary(asset_path: str, options: Any = None) -> Dict[str, Any]:
    """Inspect system, material or Blueprint data for the asset's actual type.
    Loads the requested visual-effect asset and selects the typed summary appropriate to its actual class. The result supplies asset identity and the class-specific data needed before duplication or conversion.
    AssetPath: Unreal object path of the supported effect-related asset.
    OptionsJson: JSON object; pass {} to use the defaults. Selects the inspection detail supported by the asset summary implementation.
    Returns: Inspection data selected for the asset's actual type, including its identity and relevant system, material or Blueprint data."""
    return await get_connection().send_command(
        "MCP_EffectAssetReadSummary",
        {"AssetPath": asset_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def effect_asset_export_text(asset_path: str, options: Any = None) -> Dict[str, Any]:
    """Export asset properties and dependencies for comparison.
    Loads the asset, includes reflected property/dependency information in its typed summary and produces a deterministic text representation for detailed comparison. write_file controls writing the text artifact in addition to returning it.
    AssetPath: Unreal object path of the effect-related asset to export.
    OptionsJson: JSON object; pass {} to use the defaults. write_file selects whether to write the exported text to disk.
    Returns: The inspected asset summary and comparable text; text_path is included when a file is written."""
    return await get_connection().send_command(
        "MCP_EffectAssetExportText",
        {"AssetPath": asset_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def effect_asset_soft_delete(asset_path: str, options: Any = None) -> Dict[str, Any]:
    """Check asset references before moving it to the trash folder.
    Builds a trash-move report, checks hard/soft referencers and blocks a referenced asset unless the explicit corresponding override is supplied. The default dry run returns the proposed destination; an executable request uses AssetTools to move the asset and reports the outcome.
    AssetPath: Unreal object path of the existing effect-related asset.
    OptionsJson: JSON object; pass {} to use the defaults. dry_run defaults to true; preview_only also requests a preview. trash_package_path/trash_root select the destination. allow_referenced and allow_unsafe_asset_move/force explicitly opt into the supported exceptional moves.
    Returns: A trash-move report: applicable and blocked_reason explain executability, trash_path is the proposed destination, and moved records execution when requested."""
    return await get_connection().send_command(
        "MCP_EffectAssetSoftDelete",
        {"AssetPath": asset_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def effect_duplicate_asset(source_asset_path: str, new_asset_name: str, package_path: str, save_assets: bool = True) -> Dict[str, Any]:
    """Preserve the source effect by creating a copy for conversion.
    Uses AssetTools to duplicate the source asset into the requested package and name. The duplicate preserves the source-owned visual data. When left unsaved, it is tracked by this editor session so the matching discard tool can roll it back.
    SourceAssetPath: Unreal object path of the existing source effect asset.
    NewAssetName: Name of the new duplicate asset.
    PackagePath: Destination Unreal package folder, such as /Game/Effects.
    bSaveAssets: Whether to save the assets modified or created by this operation to disk.
    Returns: The duplicate's object path and package, with saved indicating whether it has been persisted."""
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
    """Inspect effect settings inherited by newly placed renderers.
    Reads the class-default object of the selected MassBattle FX renderer class. These defaults are the authoring settings inherited by newly placed renderer actors.
    TargetClassPath: Unreal Blueprint/class path resolving to AMassBattleFxRenderer or a derived class.
    Returns: The class defaults in defaults, including its Blueprint/default object, effect system, Burst channel, subtype, batch size and cooldown; unassigned asset references are empty strings."""
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
    """Set shared effect systems, channels and batch settings for newly placed renderers.
    Updates the renderer class-default object, recompiles its Blueprint when applicable, marks the package dirty and optionally saves it. Empty system/channel paths keep the existing reference; numeric sentinel values keep the corresponding existing setting.
    TargetClassPath: MassBattle FX renderer Blueprint/class path whose defaults will be edited.
    NiagaraSystemPath: NiagaraSystem object path; an empty string keeps the existing system.
    NdcBurstFxPath: NiagaraDataChannelAsset object path; an empty string keeps the current Burst channel.
    SubType: FX subtype index. A negative value keeps the existing index.
    RenderBatchSize: Positive batch size, or a non-positive value to keep it. The MCP tool default is 2048.
    PoolingCooldown: Pooling cooldown in seconds; a negative value keeps it. The MCP tool default is 3.0.
    bSaveAssets: Whether to save the assets modified or created by this operation to disk.
    Returns: The effective renderer class and effect settings after the update, including system, channel, subtype, batch size and cooldown; saved identifies persistence."""
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
    """Discover Niagara query and editing operations in the connected editor.
    Returns native Niagara tool capabilities and parameter names for querying, inspecting, comparing and editing existing systems. The entries are graph/property primitives used to construct an explicit authoring workflow.
    Returns: The Niagara API version and operating model, together with supported inspection and graph-editing tools."""
    return await get_connection().send_command("MCP_NiagaraGetApiStatus")


@mcp.tool()
async def niagara_query(query: Any) -> Dict[str, Any]:
    """Locate Niagara systems by name or folder.
    Scans NiagaraSystem assets under the requested root, filters by path/name text and sorts candidates by object path. matched counts all query matches and returned records the bounded result count.
    QueryJson: JSON object with query text, path/root and limit.
    Returns: The matching systems' names and paths; matched is the total match count and returned is the count after applying the result limit."""
    return await get_connection().send_command("MCP_NiagaraQuery", {"QueryJson": _json_arg(query)})


@mcp.tool()
async def niagara_read_summary(system_path: str, options: Any = None) -> Dict[str, Any]:
    """Inspect the system's emitters, renderers and user parameters.
    Loads the Niagara system and summarizes emitters, renderers, user parameters and compile/authoring information. include_modules controls whether function-call module summaries are included.
    SystemPath: Unreal object path of the NiagaraSystem asset.
    OptionsJson: JSON object; pass {} to use the defaults. include_modules selects function-call module summaries.
    Returns: An emitter, renderer, user-parameter and compile summary, with module summaries included when include_modules is enabled."""
    return await get_connection().send_command(
        "MCP_NiagaraReadSummary",
        {"SystemPath": system_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def niagara_read_module(system_path: str, selector: Any) -> Dict[str, Any]:
    """Identify a module and its pins before editing them.
    Collects the system function-call module nodes and returns the first record matching the provided emitter/module/GUID/index filters. The module record includes its pins for a precise later pin edit.
    SystemPath: Unreal object path of the NiagaraSystem asset.
    SelectorJson: JSON object with optional emitter, module/function_name, node_guid and module_index/index. Use a GUID or collected module index to identify a specific matching node.
    Returns: The selected module and its pins for subsequent inspection or editing; a selector with no matching node produces an error."""
    return await get_connection().send_command(
        "MCP_NiagaraReadModule",
        {"SystemPath": system_path, "SelectorJson": _json_arg(selector)},
    )


@mcp.tool()
async def effect_discard_unsaved_duplicate(asset_path: str) -> Dict[str, Any]:
    """Roll back effect copies left unsaved in this editor session.
    Discards a duplicate only when this MCP editor session tracked it as unsaved, it is still loaded in a dirty /Game/ package, and it has not crossed the persisted-asset boundary. This supports rolling back a failed conversion before saving.
    AssetPath: Exact object path returned by the duplicate tool for an unsaved duplicate in this editor session.
    Returns: The discarded asset and package, with discarded indicating removal and deleted_count reporting the number removed.
    Before calling: Create the unsaved duplicate with UMassBattleEffectAssetMCPApi::MCP_EffectDuplicateAsset using bSaveAssets=false."""
    return await get_connection().send_command(
        "MCP_EffectDiscardUnsavedDuplicate",
        {"AssetPath": asset_path},
    )


@mcp.tool()
async def niagara_read_graph(system_path: str, selector: Any = None) -> Dict[str, Any]:
    """Obtain node, pin and link IDs for precise graph edits.
    Reads system or standalone NiagaraScript graphs and returns stable node/pin identifiers and explicit links. Optional stack-input topology and compile-state reports provide the identities and diagnostics used by the graph-edit tool.
    SystemPath: Unreal object path of a NiagaraSystem or supported NiagaraScript asset.
    SelectorJson: JSON object; pass {} to use the defaults. include_nodes, include_pins, include_stack_inputs and include_compile_state select returned detail; max_nodes bounds exported graph nodes. Graph selection fields choose the relevant script/emitter scope.
    Returns: Script graphs in the selected scope; node, pin, connection, stack-input and compile details are included according to the selectors."""
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
    """Check whether a copy or conversion preserves the source system's structure.
    Loads two different systems and compares source-neutral semantic fingerprints. exact mode checks a duplicate; translation mode applies the configured preservation rules for an intended conversion. Compile readiness/error/warning options control the corresponding result checks.
    SourceSystemPath: Unreal object path of the reference NiagaraSystem.
    TargetSystemPath: Unreal object path of a different NiagaraSystem to compare against the source.
    OptionsJson: JSON object; pass {} to use the defaults. mode must be exact or translation. require_ready_to_run, require_no_compile_errors and require_no_warnings select compile-state requirements.
    Returns: Source-preservation findings, source/target emitter and renderer counts, unexpected removed links and compile readiness."""
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
    """Inspect system and emitter properties together with module pin values.
    Combines the system summary with reflected system/emitter-data properties and every function-call module and pin. This provides the current source-aligned values before forming a property or pin edit.
    SystemPath: Unreal object path of the NiagaraSystem asset.
    OptionsJson: JSON object; pass {} to use the defaults. max_properties bounds reflected properties exported for each inspected object.
    Returns: The system summary, reflected system/emitter properties and detailed function-call modules and pins."""
    return await get_connection().send_command(
        "MCP_NiagaraReadAll",
        {"SystemPath": system_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def niagara_export_text(system_path: str, options: Any = None) -> Dict[str, Any]:
    """Produce comparable text describing Niagara system structure.
    Builds a deterministic text representation of the system summary and detailed modules for close reading and comparison. write_file selects whether the returned text is also written to disk.
    SystemPath: Unreal object path of the existing NiagaraSystem.
    OptionsJson: JSON object; pass {} for defaults. write_file selects file output.
    Returns: The system summary, detailed modules and corresponding structure text, with text_path when saved to a file."""
    return await get_connection().send_command(
        "MCP_NiagaraExportText",
        {"SystemPath": system_path, "OptionsJson": _json_arg(options)},
    )


@mcp.tool()
async def niagara_merge_write(system_path: str, patch: Any, save_assets: bool = False) -> Dict[str, Any]:
    """Patch system, emitter or renderer properties while retaining other values.
    Resolves explicit system, emitter-data or renderer targets and union-merges requested reflected property values. Omitted properties are retained. Each result records target resolution and before/after values; saving is optional.
    SystemPath: Unreal object path of the existing NiagaraSystem.
    PatchJson: JSON containing patches, or one patch object. Each patch selects a target, property/path and value_text or value. Use real reflected property names from read_all.
    bSaveAssets: Whether to save the assets modified or created by this operation to disk.
    Returns: Per-patch results identifying resolved targets, before/after values and saved state, with errors on failed patches."""
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
    """Use a node or module selector to adjust an input pin's default.
    Resolves one function-call module and named pin, then writes its default value. Linked pins require allow_linked=true because the default may be ignored; non-input pins require allow_output=true. The report identifies the selected node and old/new text.
    SystemPath: Unreal object path of the existing NiagaraSystem.
    SelectorJson: JSON using emitter, module/function_name, node_guid or module_index; allow_linked and allow_output opt into the corresponding writes.
    PinName: Exact pin name returned by module inspection.
    ValueText: Default value in the selected pin type text/import syntax.
    bSaveAssets: Whether to save the assets modified or created by this operation to disk.
    Returns: The selected emitter, module and pin, its before/after defaults, existing connection count and saved state."""
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
    """Batch-edit graph connections and stacks, then check conversion results.
    Validates ordered operation records and ids, executes explicit graph/stack edits, compiles once and checks source preservation before saving. Operations include emitter copying, user values/data interfaces, module/assignment insertion, pin connection/disconnection/rewiring and stack-input edits. Empty operations requests compile/validation only.
    SystemPath: Unreal object path of the existing NiagaraSystem.
    EditJson: JSON with an operations array (alias edits). Each object supplies op/type, optional unique id and the fields required for its operation and source-preservation policy.
    bSaveAssets: Whether to save the assets modified or created by this operation to disk.
    Returns: Ordered edit outcomes, compile diagnostics, source-preservation findings and saved state; failures report the operation or validation error."""
    return await get_connection().send_command(
        "MCP_NiagaraApplyGraphEdit",
        {"SystemPath": system_path, "EditJson": _json_arg(edit), "bSaveAssets": save_assets},
    )


@mcp.tool()
async def niagara_set_emitter_enabled(system_path: str, emitter_name: str, enabled: bool, save_assets: bool = True) -> Dict[str, Any]:
    """Switch an emitter's participation and report the actual state change.
    Finds the named emitter handle, sets its enabled state explicitly, records whether it changed and optionally saves the system.
    SystemPath: Unreal object path of the existing NiagaraSystem.
    EmitterName: Name of the existing emitter handle.
    bEnabled: Desired enabled state of the emitter.
    bSaveAssets: Whether to save the assets modified or created by this operation to disk.
    Returns: The emitter's before/after enabled state; changed identifies an actual state change and saved identifies persistence."""
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
    """Remove renderers or user parameters, or disable an emitter.
    Executes explicit renderer removal, user-parameter removal or emitter disabling on the selected system. Each action identifies its target and reports the actual removed/changed result.
    SystemPath: Unreal object path of the existing NiagaraSystem.
    DeleteJson: JSON selecting type=renderer, user_parameter or disable_emitter. Renderer removal selects emitter and renderer_index/index; user_parameter removal requires name; disabling names an emitter.
    bSaveAssets: Whether to save the assets modified or created by this operation to disk.
    Returns: The processed target and its removal or disable result, including actual removed/changed state and persistence."""
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
    """Add a sprite renderer with material and visibility bindings to an existing emitter.
    Allocates and configures a sprite renderer on an existing emitter, including its material, user-parameter bindings, sort order and visibility. It adds the renderer without rewriting the emitter graph, then finishes compilation and optional saving.
    SystemPath: Unreal object path of the existing NiagaraSystem.
    EmitterName: Name of the existing emitter receiving the new renderer.
    RendererJson: JSON with material and optional material_user_parameter, enabled_user_parameter, sort_order and renderer_visibility.
    bSaveAssets: Whether to save the assets modified or created by this operation to disk.
    Returns: The new renderer's add/compile report and persistence result; failures identify target, material or creation problems."""
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
