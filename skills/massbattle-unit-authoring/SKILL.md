---
name: massbattle-unit-authoring
description: Create, inspect, edit, delete, and validate MassBattle unit assets through the MassBattleEditorMCP/Unit MCP workflow in UE5. Use when Codex needs to manage MassBattle AgentConfig units, generate VAT/static-mesh units from SkeletalMesh assets or Actor Blueprint assemblies with modular parts and socket-bound weapons, apply style defaults, bind renderer/Niagara/projectile/sound attack data, or batch-edit unit JSON without direct .uasset mutation.
---

# MassBattle Unit Authoring

Use UE editor APIs, commandlets, or the MassBattleEditorMCP tools for `.uasset` changes. Keep names and paths aligned with existing MassBattle source conventions. For a new unit, select a style or use the default style. For an existing unit, apply a small diff.

## Project Contract Discovery

Before choosing a unit name, package path, scale, balance value, damage, defence, or historical input, search the current project for an explicit unit-design contract and read it completely. In the Winyunq project, `Docs/MassBattle/Winyunq_MassBattleFrame_Unit_Design.md` is the mandatory source of truth and has higher priority than this generic skill, style defaults, template values, generated manifests, and existing legacy assets.

Use this authority order for project decisions:

1. the user's current explicit instruction;
2. the project's canonical unit-design contract and its machine-readable manifests;
3. unit-family supplement documents;
4. this generic workflow skill;
5. style defaults, demo templates, and legacy asset examples.

Templates may supply UObject class/layout and required structural fields only. Do not inherit their unit name, directory, scale, movement, range, attack cadence, damage, defence, or presentation naming. A source-model ID is not a formal `unit_name`. Existing paths containing implementation terms such as `MCPGenerated`, `MCP`, or `AgentConfig` are migration inputs, not naming precedents, unless the project contract explicitly says otherwise.

## Combat Authoring Routing

Choose gameplay authority before editing any visual array:

```text
Direct/melee/hitscan/instant area damage
    -> $massbattle-instant-damage-fx
    -> Agent TimeOfHitAction applies damage
    -> one-shot launch/impact visuals use MassBattle Burst Batch FX

Bullet/shell/grenade/missile/arrow/homing or interceptable shot
    -> $massbattle-projectile-authoring
    -> projectile DataAsset applies damage
    -> Agent TimeOfHitAction is normally None
    -> flight uses Attached Batch FX; launch/hit/removal use Burst Batch FX
```

Use `$massbattle-effect-mcp` only for the source-faithful Niagara/Batch-FX asset conversion after the combat path is chosen. Do not treat `Attack.SpawnFx` and `Attack.SpawnProjectile` as interchangeable presentation choices.

Before writing `Attack.SpawnProjectile`, read and validate the referenced `UMassBattleProjectileConfigDataAsset` with Projectile MCP. Wiring a reference through Unit MCP does not prove that movement, collision, damage, or lifecycle fields are correct.

## Inspect Or Edit

1. List or locate units with the Unit MCP discovery tools, scoped to style roots such as `/Game/Unit`, `/Game/Toon_Soldiers_WW2`, `/Game/StylizedArmyPackA`, and `/Game/World_Flags`.
2. Read normal unit data with simple JSON first. Request detailed mode or a specific object only for complex fields such as `Attack`, `Visualize`, `LODShared`, or `AnimShared`.
3. Pass the Actor Source or unit DataAsset path as the target. Apply the explicit diff through `MCP_UnitPlanMergeUpdate` then `MCP_UnitApplyPlan`. Keep the existing values and assets for all omissions. Include `options.export_source=true` in a source edit plan when the request includes regenerating the linked unit. Use `MCP_UnitDelete` for planned deletes.
4. For array fields such as `Attack.SpawnProjectile`, `Attack.SpawnFx`, and `Attack.PlaySound`, union merge can append missing elements by default. Pass `append_arrays=false` only when append should be rejected.

## Organize Or Clean Up Assets

Treat MCP as the asset-registry hand, not as judgement. Build the relationship graph before deleting or moving anything.

1. Start from the unit with `MCP_UnitGet`, then inspect its linked renderer and generated assets with `MCP_EffectAssetReadSummary(include_dependencies=true, include_referencers=true)`.
2. Treat `missing_hard_dependencies` and `missing_soft_dependencies` as blockers. Repair or replace references before cleanup.
3. Preview linked-asset organization with `editor_apply_organize_unit_assets(unit_path, options={"dry_run": true})`. Review `plan.moves`, `plan.blocked_count`, and referencers before using `dry_run=false`.
4. `MCP_EffectAssetSoftDelete` is a review tool by default. It blocks live asset moves unless `allow_unsafe_asset_move=true` is explicitly supplied after referencers and editor state are reviewed.
5. Keep one canonical generated asset set per playable unit. Old generated folders, tests, and marketplace source folders are duplicates until their referencers prove otherwise.

## Create Unit

Build VAT assets with the resolved `options` for the build. Use `editor_list_styles` and `editor_get_style` to inspect creation defaults. Omitting `style_profile` selects the default style.

### Create From An Actor Blueprint

Use `editor_apply_create_vat_unit_from_actor` when an Actor Blueprint defines the final appearance through modular SkeletalMesh components, StaticMesh weapons, or component-specific material and mesh choices. This wrapper assembles one persistent SkeletalMesh, then delegates to the strict unit workflow.

1. When component names or weapon bindings are unknown, call `editor_inspect_actor_assembly` first. Treat `can_assemble=false` or any error in `issues` as a blocker.
2. Supply `actor_class` and the intended output in `options`. For a new unit, supply the inputs that the selected style leaves open. When explicitly rebuilding presentation assets for an existing unit, use its path as `target_unit` and identify the build inputs.
3. Use `component_overrides` entries keyed by canonical `component`. A SkeletalMesh component can receive `skeletal_mesh`; a StaticMesh component can receive `static_mesh`; mesh components can receive `materials`. Use `visible`, `clear_mesh`, and `bind_bone` only when the Actor defaults are not the intended output.
4. Set `root_component` only when automatic root selection is ambiguous. All included modular skeletal parts must use compatible skeletons and share the root's model space.
5. Keep attached StaticMesh vertices in the root skeleton's reference-pose model space. The converter composes `ComponentRelative * SocketLocal * ReferenceBoneToRoot` and rigidly weights the attachment to the resolved bone. Do not add a manual left/right-hand, axis, or evaluated-pose correction unless the source Actor itself is authored incorrectly.
6. For a new unit, use the style to fill omitted build values. Supply compatible animations and a positive `vat_sample_rate` if the chosen style leaves them open.
7. Call `editor_apply_create_vat_unit_from_actor` directly. Use `editor_plan_create_vat_unit_from_actor` only for an explicit dry run or after an apply reports a planning error. Pass `overwrite_existing=true` when replacing an existing assembled mesh or generated output.
8. Require top-level `success=true`, then inspect `assembly`, `resolved_options`, and `vat_result`. Read the generated AgentConfig with `unit_get`. Use `editor_inspect_vat_animation` when animation, skin weights, lookup UVs, or VAT material configuration needs verification.

### Create From Selection Or A Skeletal Mesh

1. Call `editor_plan_create_vat_unit_from_selection` when generation starts from selected assets. Review the selection, warnings and `resolved_options`, especially the target unit. Pass `resolved_options` as `options` to `editor_apply_create_vat_unit`.
2. For a supplied skeletal mesh, pass its path and the requested output options to `editor_apply_create_vat_unit`.
3. Use `MCP_EditorPlanCreateVatUnit` or `MCP_EditorValidateCreateVatUnit` for separate diagnostics or when the user requests a dry-run plan.
4. Inspect `validation.issues` and `execution_steps` from the apply result. Use completed steps to identify any assets already modified when execution stops.
5. Pass an explicit `animations` category map when the required animation mapping is unavailable from the selected inputs and style.
6. Read back the generated unit with `MCP_UnitGet` and verify asset existence, renderer class, material slot, attack enabled state, projectile/effect arrays, range, damage, and subtype.
7. For VAT materials, expect filename discovery first and source-material texture inheritance as a fallback. Treat `defaulted_original_textures_from_source_material` warnings as review items: verify the resulting material depends on the intended BaseColor/Normal/ARM textures with `MCP_EffectAssetReadSummary`.
8. Use `unit_name`, `target_package_path`, and `target_unit_package_path` for output names and paths.

Official DoAll correspondence:

- `FillTex` -> `FindAndFillOriginalTextures`, with source-material inheritance when filename discovery is incomplete.
- `FillLod` -> `FindAndFillLODSettings`.
- `FillAnim` -> `FindAndFillAnimSequences`, explicit `animations`, or compatible-animation fallback with warnings.
- `CreateStaticMeshAndMat` -> `ConvertSkeletalMeshToStaticMeshWithLODs` then `CreateMaterialInstanceForStaticMeshWithLODs`.
- `CreateVATTextures` -> create/update `UAnimToTextureDataAsset`, create VAT textures, run `AnimationToTexture`, update material instances.
- `CreateDataAsset` -> merge `Visualize`, `LODShared`, `AnimShared`, and runtime animation sample-rate defaults into the target unit.
- `CreateRenderer` -> duplicate/update the renderer Blueprint defaults for mesh, Niagara system, and subtype.

## Strict Create

The editor MCP must reject incomplete VAT unit creation instead of generating partial assets.

- Before a VAT build, review the resolved mesh, unit name, output paths, material, renderer, Niagara, sample rate, and animations.
- For an ordinary edit to an existing unit, use the unit diff tools. For an explicit VAT rebuild, pass `target_unit` with the required build inputs.
- For new units, supply a destination and any required creation inputs that the style leaves open.
- Use `overwrite_existing=true` and `refresh_materials=true` when refreshing generated assets. Existing generated StaticMeshes block strict create when overwrite is false.
- Do not pass old aliases such as `unit_path`, `existing_unit`, `mesh`, `source_mesh`, `sub_type`, `renderer_template_class`, `template_renderer_class`, `niagara`, `sample_rate`, or `VATSampleRate`.
- Do not pass fallback switches such as `allow_static_fallback`, `allow_missing_anims`, `allow_no_vat`, or `allow_missing_renderer`.
- `MCP_EditorApplyCreateVatUnit` runs `MCP_EditorValidateCreateVatUnit` before any asset write. Validation failure means no mesh, material, VAT texture, renderer, or unit data is modified.
- The selection planner accepts `selected_assets` and also considers the current editor selection. Review the resolved target before applying. Pass `selected_unit_role="template"` when the selected unit should serve as the template for a new unit.

## Defaults

After resolving the project contract, use style to fill open values on new units, such as renderer or LOD defaults and common projectile, effect, and sound bindings. Explicit project values for package roots, names, scale, and balance take precedence. Keep per-unit options small:

- `material_overrides` for skins or flag/country variants.
- `source_renderer_class` and `niagara_system` for renderer binding.
- `unit_patch` for gameplay changes such as `SubType`, `Trace`, `Attack`, `Damage`, and `Visualize`.

## Project-Specific Naming And Balance

Do not duplicate a project's conversion formulas, naming examples, or current values in this skill. Read them from the canonical project contract and its machine-readable inputs on every task. This prevents a cached skill example from silently overriding a newer project rule.

## Verification

Run the UE editor commandlet for deterministic validation when possible. A successful create pass should show `plan_success`, `validate_valid`, `apply_success`, `read_after_success`, and asset existence as true. Also inspect `warnings` and `execution_steps`; do not claim exact animation/state correctness if the result used fallback animation mapping. Infantry/officer attack validation should confirm at least one projectile and one attack FX entry.
