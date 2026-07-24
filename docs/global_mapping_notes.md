
All projects
ZED
You are acting as a senior software architect, embedded systems engineer, and technical mentor. I am a computer engineering student with limited real-world software engineering process experience. I want this project to be a serious learning opportunity. Your job is not just to give me an architecture, but to teach me the end-to-end software engineering and software architecture process in a practical, non-bloated way.
Show more

Claude Fable 5 is currently unavailable.
Learn more(opens in new tab)



Type / for skills


Update global mapping notes for fixed FrameBundle
Last message just now
Adding ZED positional tracking to global mapping notes
Last message 3 minutes ago
Dynamic traversability map dimensions from config
Last message 45 minutes ago
Understanding serialization
Last message 1 hour ago
Dynamic traversability map dimensions from config
Last message 2 hours ago
ZED mapping explained
Last message Jun 1
Planning camera pose and tracking state data transmission
Last message Jun 1
Coding agent prompt for ZED tracking state monitoring
Last message Jun 1
Refactoring FrameData with aggregate result container
Last message May 8
Debugging unexpected shutdowns in C++ applications
Last message May 8
Qt multi-view visualization architecture for polar grid and point cloud
Last message May 8
Extending traversability pipeline with optional debug outputs
Last message May 7
Minimal Docker container for NVIDIA Jetson Xavier
Last message Apr 20
UDP traversability result transmission
Last message Apr 17
Decoupled metrics architecture for pipeline performance validation
Last message Apr 17
Implementing traversability pipeline from architecture specification
Last message Apr 17
Voxel filtering timing in processing pipeline
Last message Apr 16
Component decomposition document
Last message Apr 16
Architecture drivers documentation
Last message Apr 16
Intentional architecture for existing code
Last message Apr 10
Instructions
You are a senior software architect and mentor guiding a student through designing a real system. Your goal is to **teach the software engineering and architecture process step by step**, not to jump directly to a final solution. ## How to guide me * Work in **phases** (requirements → design → architecture → validation → etc.) * At each step: * Explain **why this step matters** * Ask me the **key questions I need to answer** * Help me produce a **small, useful artifact** (not over-engineered) * Keep everything **practical and lightweight** ## Important constraints * This is a **single-developer project** * Target system: **Jetson Xavier + ZED camera + C++/CUDA pipeline** * Real-time / streaming considerations are important * GUI is **out of scope for now** ## Teaching style * Be concrete and example-driven * Prefer **simple first, then refine** * When multiple options exist: * explain tradeoffs briefly * recommend one and justify it * Call out when something is: * essential * optional * overkill ## Output expectations * Do NOT give everything at once * Guide me **interactively**, one step at a time * For each artifact: * give a short template * then help me fill it in ## Goal By the end, I should: * understand the architecture decisions * have a small set of meaningful documents * be able to explain *why* the system is designed that way

Files
1% of project capacity used

consumers.md
90 lines

md



COMPONENTS.md
133 lines

md



result.md
78 lines

md



global_mapping_notes.md
134 lines

md



pipeline.md
61 lines

md



traversability_architecture.docx
265 lines

docx



Current Codebase Layout
52 lines

text


Traversability .pdf
pdf


global_mapping_notes.md


# Global Mapping from the Traversability Pipeline
 
## What the pipeline produces per frame
 
**Update:** The pose gap noted below is resolved. The pipeline now uses the ZED SDK's positional tracking module (with loop closure), and per-frame output has been consolidated into `FrameResult` (`include/traversability/frame_result.hpp`), which is what `IResultConsumer::consume()` now receives.
 
`FrameResult` contains:
 
- `traversability` (`TraversabilityResult`) — `trav_grid`, `r_edges`, `theta_edges`, `r_bins`, `theta_bins`, in the **camera's local polar frame**
- `camera_pose` (`CameraPose`) — `tx, ty, tz` + `qx, qy, qz, qw`, the camera's position and orientation in **world space**, from the ZED positional tracking module
- `tracking_state` (`TrackingState`) — tracking status at capture time: `OK`, `SEARCHING`, `FPS_TOO_LOW`, `SEARCHING_FLOOR_PLANE`, `UNAVAILABLE`, `LOOP_CLOSED`
- `timestamp_ns` — frame capture time
- `has_image` / `image` (`ImagePayload`) — optional JPEG-encoded camera frame
The traversability grid is still **local** — the polar origin is the camera. To build a global map, transform it into a shared world frame using `camera_pose`.
 
---
 
## Architecture decision: global mapping runs on the receiver, not the Jetson
 
The global map is built **on the Qt receiver application**, not as an `IResultConsumer` on the Jetson pipeline.
 
**Why:**
 
- The Jetson pipeline is real-time constrained — `PipelineRunner` already has a tight per-frame budget (stages → `cudaStreamSynchronize` → publish). Adding global-map fusion to that thread risks frame drops in the perception pipeline itself.
- The receiver already gets everything needed to build the map: `trav_grid`, `r_edges`/`theta_edges`, `camera_pose`, `tracking_state`. No new data needs to be sent.
- Network traffic stays bounded. The local trav grid (17×19) is fixed-size; a *growing* global map would not be. Sending the local grid + pose every frame and fusing on the receiver is strictly cheaper than computing the global map on the Jetson and sending *that*.
- Fusion rule changes (overwrite → max-danger → log-odds) happen on the receiver — no Jetson redeploy or CUDA rebuild needed.
The local→global transform and fusion logic described below all run receiver-side, in Qt, fed by the comm messages from `CommMapSender`.
 
### The one issue: libcomm mailbox semantics

~~libcomm mailboxes keep only the latest message per mailbox. If `trav_grid`, `camera_pose`,
`tracking_state`, and `timestamp_ns` are sent as **separate messages/mailboxes**, the receiver
can read a trav grid from frame N alongside a pose from frame N+1 — they're not guaranteed to
arrive or be read together. For global mapping this is a real correctness issue: pairing the
wrong pose with a trav grid places that frame's obstacles at the wrong world location, silently
corrupting the map.

**Solution:** bundle `trav_grid` + `camera_pose` + `tracking_state` + `timestamp_ns` into a
**single message on a single mailbox**, so the receiver always reads a consistent (grid, pose,
state, time) tuple per frame. The image, if sent, can stay on its own mailbox — it isn't used in
the fusion math and a stale/missing image frame doesn't corrupt the map.~~

**Handled:** resolved via the bundled `FrameBundle` on mailbox 200 — see `consumers.md`.
---
 
## The core problem: local → global
 
For each frame, each polar bin `(i, j)` maps to a real-world point. The bin centre in the camera frame is:
 
```
r     = (r_edges[i] + r_edges[i+1]) / 2
theta = (theta_edges[j] + theta_edges[j+1]) / 2
 
x_cam = r * cos(theta)
y_cam = r * sin(theta)
z_cam = 0  # no per-cell height is available from the wire format
```
 
Transform to world frame using `camera_pose` (rotation from `qx,qy,qz,qw` + translation `tx,ty,tz`), then snap to the nearest cell in a fixed-resolution world grid. Apply your fusion rule. Repeat every frame.
 
### Handling `tracking_state`
 
With loop closure enabled, `camera_pose` can jump discontinuously when `tracking_state == LOOP_CLOSED` (a correction is applied retroactively to the pose estimate). On the receiver:
 
- `OK` / `LOOP_CLOSED` — pose is good, fuse normally. A `LOOP_CLOSED` event may mean previously-fused cells were placed using a now-corrected pose; for the overwrite/max-danger approaches this is an accepted approximation, not something to fix immediately.
- `SEARCHING`, `FPS_TOO_LOW`, `SEARCHING_FLOOR_PLANE`, `UNAVAILABLE` — pose is unreliable or absent. Skip fusion for this frame rather than writing into the global map using a bad/missing pose.
---
 
## Fusion approaches, simplest to most principled
 
**Overwrite (start here)**
Last observation wins. Simple, fast. A cell flips between traversable and obstacle freely. Good for verifying the geometry is correct before worrying about noise.
 
**Max-danger**
Keep the highest danger score seen per cell across all frames. A cell is only traversable if it has never been seen as an obstacle. Conservative — good for safety-critical navigation, tends to overmark obstacles near sensor range edges.
 
**Bayesian / log-odds**
Each observation updates a probability estimate. Standard occupancy grid formulation. Handles sensor noise gracefully, converges over repeated observations. Requires tuning a sensor model (probability of false positive / false negative). This is the textbook approach.
 
**Elevation fusion with uncertainty (Fankhauser et al.)**
Instead of fusing the traversability classification, fuse the raw height values with a per-cell variance estimate. Derive traversability from the fused elevation map. More robust to single bad frames. Higher implementation cost.
 
For a single developer starting out: overwrite first, max-danger second, log-odds if you need noise robustness.
 
---
 
## Literature to read
 
**The foundational paper**
Elfes, A. (1989). *Using occupancy grids for mobile robot perception and navigation.* Computer, 22(6), 46–57.
The original formulation. Short and readable. Establishes the probabilistic update rule everything else builds on.
 
**The textbook treatment**
Thrun, S., Burgard, W., & Fox, D. (2005). *Probabilistic Robotics.* MIT Press. — Chapter 9.
Chapter 9 is the one chapter you need. Covers occupancy grids end to end including the log-odds formulation. Freely available via many university libraries.
 
**Closest match to your setup**
Fankhauser, P., Bloesch, M., & Hutter, M. (2018). *Probabilistic Terrain Estimation for Legged Robots.* IEEE Transactions on Robotics, 34(6).
They maintain a robot-centric elevation map from a depth camera, fusing frames with per-cell uncertainty. The `elevation_mapping` ROS package is open source. Read the paper before the code.
 
**Traversability survey**
Papadakis, P. (2013). *Terrain traversability analysis methods for unmanned ground vehicles: A survey.* Engineering Applications of Artificial Intelligence, 26(4), 1373–1385.
Covers slope/roughness/step-height metrics (exactly what your pipeline computes) and how they're typically aggregated. Good context for understanding the choices already made in your traversability stage.
 
---
 
## Questions to work through before building this
 
**About your data**
 
- What resolution do you want the global map to be? Your local grid bins are ~0.1 m radially. Does the global grid need to match, or can it be coarser?
- How large an area does the robot operate in? A 20×20 m map at 0.1 m resolution is 200×200 cells — trivial. A 200×200 m map starts to matter for memory.
- Do you need the map to persist across runs, or is per-session sufficient for now?
**About pose quality**
 
- How good is the ZED SLAM pose in your environment? A global map is only as accurate as the pose used to build it. In a low-texture environment (plain walls, outdoor terrain), the pose will drift and your map will smear.
- `tracking_state == SEARCHING_FLOOR_PLANE` (or other non-`OK`/`LOOP_CLOSED` states) means the pose isn't yet reliable. Frames captured during this state should be skipped — see "Handling `tracking_state`" above.
- `LOOP_CLOSED` events retroactively correct drift. Should already-fused cells be re-fused with the corrected pose, or is the smear an accepted limitation for now?
**About the fusion rule**
 
- Can a cell that was previously marked as obstacle become traversable? (e.g. a person walking through the scene.) If yes, you need a forgetting mechanism — log-odds handles this naturally, overwrite does too. Max-danger does not.
- How do you handle cells that have never been observed? NaN passthrough is the right default, but your navigation planner needs to know how to treat unknown cells.
**About the architecture**
 
- The global map lives in the Qt receiver process, fed by the bundled comm message (trav_grid + pose + tracking_state + timestamp). No Jetson-side `IResultConsumer` changes needed beyond the bundled message format in `consumers.md`.
- The global map itself is mutable state owned by whatever thread processes incoming comm messages in the Qt app. If anything else needs to read it (a UI panel, a planner on a different machine), plan for a lock or a second handoff — don't design this until you know who the reader is.
---
 
## Suggested first step
 
1. ~~Add `camera_pose` (and `timestamp_ns` if not already there) to `TraversabilityResult`~~ — **done**, via `FrameResult` (see `frame_result.hpp`).
3. In the Qt receiver, write a global map module that receives the bundled message, checks `tracking_state`, transforms each bin to world XY, and writes into a flat `float` array using the overwrite rule.
4. Verify the geometry looks correct before adding any fusion sophistication.
 
