# Component blueprint reconciliation

Status: **draft design linkage; no implementation or accepted runtime result**. The consolidated entry point for this discussion is [NATIVE_WORKSPACE_BUILD_MAP.md](NATIVE_WORKSPACE_BUILD_MAP.md), under PR #272 and the existing #68/#268/#65/#66/#67 owners. The [review index](README.md) starts there.

Two overlapping blueprints were published into the same review branch during this discussion. They describe the same native, user-benefiting, locally retained substrate and universal-control architecture. Do not implement them as two registries, two local stores, two query coordinators, two sets of controls or two work queues.

## Preserved full source

The full 360-line prior blueprint is retained unchanged at [review revision 091143a927c3ac0620c5598622cd29074d721132](https://github.com/SaltyPatron/Laplace-Refactor/blob/091143a927c3ac0620c5598622cd29074d721132/docs/product/ui/GENERIC_COMPONENT_AND_NODE_BLUEPRINT.md), blob `ccd373121a67fedac8ab52f4f6f84f8cab7e0b5e`. This pointer preserves all source text, examples, references and criteria. Reconciliation is not feature completion, design approval or removal of the user's required behavior.

## One work-package map

Use the current build map's W identifiers for execution planning. Earlier G identifiers remain references to the preserved draft, not additional tasks.

| Prior draft | Current owning work |
|---|---|
| G01 Effective descriptors | W01 Effective descriptors and exact marshaling |
| G02 Native client execution | W02 Native local host; #66/#67 qualify targets, #5/#10 retain core/bindings |
| G03 Local store/presence | W03 Local data/perfcache; #14/#15 retain native store/index semantics |
| G04 Targeted exchange/heads | W04 under #65 and #268; current grants remain #64-owned |
| G05 Query/result coordination | W05 under #268 and native #17/#60 |
| G06 Primitive controls/grid | W06 under #68 |
| G07 Workspace/layout/selection | W07 under #68/#172/#176 |
| G08 Rich surfaces/contributors | W07 rich views plus W08 measure/contributor behavior |
| G09 Actions/jobs/recovery | W09 with existing #264/#265/#266 |
| G10 Local privacy/sync experience | W03/W04/W09 with #64/#65; no separate account or sync engine |
| G11 Module packaging/DX | W10 domain packages and W11 public/platform delivery |
| G12 Installed journeys/performance | Evidence accompanying every W package under #22/#54, not a final UI bolt-on |

## Additional detail retained for the current build map

These points supplement its named contracts/controls without creating parallel types or implementations:

- **ViewRecipe:** compatible input shape/roles, named layout slots, bindings, pane priority/minimum usable size, responsive transition, selection mapping and presentation options. This is the task-layout contract used by WorkspaceFrame, not a second domain page system.
- **NodeCapability:** loaded package/ABI, execution/storage providers, qualified numeric profiles, actual topology/resource limits and reachable data scopes. This supplies the target/pack resolver and native placement plan.
- **ResidencyPolicy:** owned/replica/derived class, namespace, pin/eviction policy, dependency versions, offline/disclosure constraints and quota. It supplies the local record/device-data providers, not a second storage schema.
- **AlignmentView:** ordered input references, an admitted alignment result and correspondence locators. Use the existing sequence/compare controls for translation, source/AST, DNA and structural alignments; the UI must not calculate a private alignment.
- **FormulaSurface:** declared mathematical representation, renderer profile, source map and selected node. It is one reusable DocumentOrCodeViewer/structure presentation adapter; typesetting does not imply proof checking, unrestricted macros or arbitrary host access.
- **ScopePicker and ActionBar:** reusable compositions of the current field/reference/query/selection and OperationDescriptor controls; do not duplicate authority or create per-source action logic.
- **Subscriber-aware shared reads:** two panes can need the same in-flight dependency. Closing one releases its subscription; it must not cancel work still needed by another pane. Cancel underlying work when its owning operation or final interested subscriber releases it under the common lifecycle.
- **Portable storage surface:** bulk presence/read/write, supported transaction/checkpoint, exact range read, staged manifest publication, pin/unpin, quota/health, verified export/import and recovery. State the actual provider durability instead of claiming browser storage equals PostgreSQL durability.
- **Marshaling cost:** use transferable or columnar buffers where the chosen host supports them, with explicit ownership and memory-growth handling. Measure actual retained/transferred/copied bytes; do not promise zero-copy across unrelated memory/process/network boundaries.
- **Selection and cell-state separation:** a data value's null/unobserved/withheld/not-applicable status is independent of a pane's loading/refreshing/failed state. Focus, hover and working-set membership are also distinct.
- **Extension dependency examples:** a LaTeX macro/include change can affect multiple rendered regions; DNA ranges need explicit coordinate convention/orientation/reference; board topologies must not assume 8x8 or one piece per location. Those requirements belong to shared locators/adapters plus actual native domain programs.

## Shared delivery rule

Deliver useful vertical slices rather than two bottom-up framework projects. The necessary native/query/provider path, generic controls, retained workspace and labels form the first real browse journey. Add local persistence and targeted exchange into that same journey, demonstrate meaningful analysis/operations, then prove unfamiliar pack and host integration without generic-core rewrites. Exact source semantics, private local ownership, evolving current results, bounded useful work and prior detailed UI/response/recovery requirements all remain.

The client works for its user. It is not a pool for unrelated provider jobs, and local caches are not permanent answers. C#/SQL/browser adapters orchestrate the same native C/C++ machine. Neither this reconciliation nor the preserved draft authorizes implementation or claims that any target, query, storage provider or UI is operational.
