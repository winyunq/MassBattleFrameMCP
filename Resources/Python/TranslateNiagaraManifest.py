"""Execute a source-faithful Niagara translation manifest against the editor bridge."""

from __future__ import annotations

import argparse
import asyncio
import json
from pathlib import Path
from typing import TYPE_CHECKING, Any, Dict

if TYPE_CHECKING:
    from MassBattleMcpServer import UnrealConnection


async def translate_manifest(
    connection: UnrealConnection,
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
            source_read = await connection.send_command(
                "MCP_NiagaraReadSummary",
                {"SystemPath": source, "OptionsJson": '{"include_modules":false}'},
            )
            if not source_read.get("success"):
                item_errors.append(f"source read failed: {source_read.get('error', source_read)}")
            else:
                target_read = await connection.send_command(
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
        cleanup = await connection.send_command(
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
        duplicate = await connection.send_command(
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
        clone_compare = await connection.send_command(
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

        edit_result = await connection.send_command(
            "MCP_NiagaraApplyGraphEdit",
            {
                "SystemPath": target,
                "EditJson": json.dumps(item["edit"], ensure_ascii=False),
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
        translation_compare = await connection.send_command(
            "MCP_NiagaraCompareSystems",
            {
                "SourceSystemPath": item["source"],
                "TargetSystemPath": target,
                "OptionsJson": json.dumps(comparison, ensure_ascii=False),
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
            save_result = await connection.send_command(
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path, help="Path to the JSON manifest")
    parser.add_argument(
        "--apply", action="store_true", help="Apply the manifest after preflight"
    )
    parser.add_argument(
        "--no-save", action="store_true", help="Leave successful edits unsaved"
    )
    args = parser.parse_args()

    try:
        manifest = args.manifest.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        result = {"success": False, "error": f"Could not read manifest: {exc}"}
    else:
        from MassBattleMcpServer import UnrealConnection

        result = asyncio.run(
            translate_manifest(
                UnrealConnection(),
                manifest,
                apply=args.apply,
                save_assets=not args.no_save,
            )
        )

    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result.get("success") else 1


if __name__ == "__main__":
    raise SystemExit(main())
