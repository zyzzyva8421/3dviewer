# OpenROAD 3D Viewer Next Phase Implementation Plan

## 1. Background And Objectives

This phase focuses on three tracks and closes them within one consistent architecture:

1. Geometric semantic consistency: keep wire/via/pin/instance semantics aligned between 3D and ODB/2D, especially centerline, thickness, and inter-layer Z relationships.
2. Capability gap closure: close key functional gaps between current 3D viewer and glview/winGLObject, prioritizing high-value and low-risk items.
3. Structural scalability: introduce a general physical-layer-shape interface to reduce coupling between OdbSceneBuilder and rendering backend.

Default code scope in this plan is tools/OpenROAD/src/gui/openroad3D.

## 2. Out Of Scope

1. No major UI redesign.
2. No full one-to-one legacy feature cloning.
3. No disruptive rewrite of the scene snapshot model.

## 3. Milestones

Use four incremental milestones. Each should be independently committable and regressible.

### M1. Semantic Baseline And Regression Guard (Week 1)

Goal: lock in current semantic fixes as a stable baseline before refactoring.

Deliverables:

1. Baseline design set (at least gcd plus one multi-layer multi-via design).
2. Geometry checklist (wire centerline, via top/bottom/cut, instance bbox and transparency).
3. Reproducible run script and screenshot comparison workflow.

Definition of done:

1. openroad_3d -odb ... exits with code 0 on baseline designs.
2. No visible center drift or thickness anomaly in 2D/3D comparison for key objects.
3. A reproducible validation note is available.

### M2. General Physical-Layer Shape Interface MVP (Week 2)

Goal: implement minimum viable abstraction without changing external behavior.

Deliverables:

1. Introduce PhysicalLayerResolver (or equivalent naming) for centralized thickness and Z resolution.
2. Introduce a unified emitter interface (for example ShapeEmitter) to convert XY plus layer semantics into ObjectRecord.
3. Replace repeated bbox/transform construction in processNets, processPins, and processInstances.

Suggested interface draft:

```cpp
struct LayerPhys {
  int layer_number;
  float z_center;
  float thickness;
};

struct ViaPhys {
  LayerPhys top;
  LayerPhys bottom;
  LayerPhys cut;
};

class IPhysicalLayerResolver {
 public:
  virtual LayerPhys resolveLayer(odb::dbTechLayer* layer) = 0;
  virtual ViaPhys resolveVia(odb::dbTechLayer* top,
                             odb::dbTechLayer* bottom,
                             odb::dbTechLayer* cut) = 0;
  virtual ~IPhysicalLayerResolver() = default;
};
```

Definition of done:

1. Core object generation path is unified through interface emitters instead of scattered per-type computation.
2. Visual output matches M1 baseline (allowing negligible floating point differences).
3. Build passes with no new critical warnings.

### M3. Functional Gap Closure (Week 3)

Goal: close the most impactful user-facing gaps found in glview/winGLObject comparison.

Priority features:

1. Visibility semantic decoupling: add visible field or backend mask, and stop using hasBbox as visibility state.
2. Standardized filter and highlight path: layer/type/net filtering should share one state model.
3. Incremental refresh preparation: establish stable object ID to render node mapping.
4. Unified debug-render toggles for wire/via/pin/instance categories.

Definition of done:

1. OpenRoad3DWindow filter behavior does not modify geometry-existence fields.
2. No object state pollution when toggling filters repeatedly.
3. Selection, filtering, and screenshot interactions are stable on baseline designs.

### M4. Rendering Policy Consolidation And Documentation (Week 4)

Goal: convert backend branching from feature accumulation into policy mapping.

Deliverables:

1. Consolidate OsgScene branching into ObjectType to GeometryPolicy mapping.
2. Centralize tolerance and minimum-size policy to avoid center drift regressions.
3. Add developer documentation for object semantics, rendering policy, debugging, and extension points.

Definition of done:

1. Adding a new object type requires only policy-layer extension.
2. Documentation enables a new developer to integrate one object type in one day.

## 4. Work Breakdown By Module

### 4.1 ODB Modeling Layer

Scope: OdbSceneBuilder.h and OdbSceneBuilder.cpp

Tasks:

1. Extract a layer-physics resolver and replace file-scope static caches.
2. Extract common helpers such as emitRectObject and emitViaTriplet.
3. Clean obsolete paths, duplicate branches, and temporary debug logic.

### 4.2 Domain Model Layer

Scope: core/domain/SceneTypes.h

Tasks:

1. Add display semantic fields such as visible, orthogonal to hasBbox.
2. Add or normalize stable object trace identifiers.

### 4.3 UI And Interaction Layer

Scope: OpenRoad3DWindow.cpp

Tasks:

1. Update filtering to operate on visibility semantics, not geometry semantics.
2. Build a clear call chain from filter state to scene update.

### 4.4 OSG Backend

Scope: render/backend_osg/OsgScene.cpp

Tasks:

1. Introduce rendering policy mapping and reduce behavior coupling from large switch blocks.
2. Centralize and parameterize tolerance and minimum-size strategy.
3. Reserve object mapping hooks for selection and highlight.

## 5. Validation And Quality Gates

### 5.1 Regression Matrix

1. Design dimension: small design (gcd) plus one medium-complexity multi-layer multi-via design.
2. Object dimension: wire, via top/bottom/cut, pin (BTerm and ITerm), instance.
3. Operation dimension: load, filter toggle, select, screenshot, view anchor switch.

### 5.2 Automation Suggestions

1. Add a minimal smoke script that loads fixed designs and checks exit code.
2. Add key log keyword scans such as error, fatal, and assert.
3. Add key object count summary output for before/after refactor comparison.

### 5.3 Manual Acceptance Checklist

1. Wire centerline is visually aligned with connected via centers.
2. Cut-layer thickness and metal-layer thickness relation is correct and no Z overlap exists between layers.
3. Instance envelope is consistent with internal pin-layer semantics and transparency is correct.
4. Filter toggles do not corrupt geometry state.

## 6. Risks And Rollback

Main risks:

1. Hidden visual regressions that still compile successfully.
2. Over-abstraction too early causing delivery slowdown.
3. Interaction regressions from filter and selection state updates.

Mitigations:

1. Keep milestones independently roll-backable.
2. Extract helpers first, then interfaces, while preserving behavior.
3. Enforce M1 baseline regression before moving to next milestone.

Rollback strategy:

1. Keep legacy path isolated until milestone completion.
2. If severe visual regression appears, rollback policy-layer changes first and keep data ingestion path unchanged.

## 7. Suggested Role Split

1. Modeling owner: OdbSceneBuilder and SceneTypes abstraction.
2. Rendering owner: OsgScene policy consolidation and highlight mapping.
3. Interaction owner: OpenRoad3DWindow filter semantics and state model.
4. Validation owner: regression scripts, screenshot comparison, and acceptance records.

## 8. Immediate MVP Tasks For This Week

1. Commit 1: add visible and decouple filtering semantics from geometry generation.
2. Commit 2: extract emitRectObject and replace duplicated net and pin paths.
3. Commit 3: extract PhysicalLayerResolver and wire it to wire and via paths first.
4. Commit 4: add short developer note describing new interfaces and extension workflow.

After these four commits, proceed with full M2 and M3 execution.
