#include "tutorials/tutorial_state.h"
namespace studio {
const std::vector<TutorialTopic> &tutorial_topics() {
    static const std::vector<TutorialTopic> topics{
        {"main/Workspace", "Workspace",
         "Choose Maps, Models, Studio, Authoring, Collision, Cameras, Images or Audio. Narrow "
         "windows use this dropdown so Project and Help remain accessible. Each workspace "
         "introduces its controls when you first explore it."},
        {"collision_editor/##visible", "Collision category visibility",
         "Show or hide this collision category in the preview. Hidden collision is still present "
         "in exported data."},
        {"control/1  Terrain", "1  Terrain",
         "Choose the terrain authoring stage to create, subdivide and shape the ground."},
        {"control/2  Surfaces", "2  Surfaces",
         "Choose the surface authoring stage to assign textures and paint ground transitions."},
        {"control/3  Objects", "3  Objects",
         "Choose the object authoring stage to place reusable assets and adjust their transforms."},
        {"control/3D cursor and ruler", "3D cursor and ruler",
         "Show tools for placing a reference cursor and measuring distances in the map. Setting "
         "the cursor alone does not move objects."},
        {"control/3DS preview (400 x 240)", "3DS preview (400 x 240)",
         "Render the viewport at 400 by 240 pixels for a low-resolution preview. This changes "
         "render resolution, not the game's assets."},
        {"control/3DS preview at native size", "3DS preview at native size",
         "Show the 400 by 240 preview without enlarging it to fill the viewport. Enable 3DS "
         "preview first."},
        {"control/4  Review", "4  Review",
         "Review the authored composition and export validation before compiling changes into game "
         "resources."},
        {"control/AMX", "AMX",
         "Inspect the interaction's underlying bytecode/disassembly for details not represented in "
         "the action list."},
        {"control/Actions", "Actions",
         "Read the interaction as a list of interpreted actions. The preview does not execute the "
         "script."},
        {"control/Add Pokemon assets", "Add Pokemon assets",
         "Create a new Pokemon model bundle or additional form from compatible donor assets. "
         "Review resource layout and species/form assignment before staging."},
        {"control/Add child bone", "Add child bone",
         "Create a new child of the selected bone. Adding a bone does not automatically weight "
         "vertices to it or animate it."},
        {"control/Add encounter region", "Add encounter region",
         "Create a new encounter region and configure its shape, ground filters and table "
         "reference."},
        {"control/Add from selected", "Add from selected",
         "Create a placement using the selected record as a template. Review its event ID, "
         "resource, position and script-related fields before applying."},
        {"control/Add material track", "Add material track",
         "Choose a material and track type to animate UV transforms, colors or texture assignment. "
         "Add the track, then select its channel to author keys."},
        {"control/Add texture from PNG...", "Add texture from PNG...",
         "Add a new texture resource from a PNG. Assign it to a material texture unit before "
         "expecting it to appear on the model."},
        {"control/Add track", "Add track",
         "Add the selected kind of animation track for the chosen material/resource. Select a "
         "channel and frame to author its keys."},
        {"control/Additional form", "Additional form",
         "Add a model form to an existing species row. Confirm the species and form indexing "
         "before creating the bundle."},
        {"control/Advanced selection", "Advanced selection",
         "Select a field area/zone directly when the named map list is insufficient. Press Load "
         "map to load the new selection."},
        {"control/All visible", "All visible",
         "Show all available meshes in this preview. Visibility used for inspection is separate "
         "from saved visibility animation."},
        {"control/All visible mesh triangles", "All visible mesh triangles",
         "Include triangles from every visible mesh in the editing view. Check the selection "
         "before applying an operation across parts."},
        {"control/Animate", "Animate",
         "Enable this kind of animation in the viewport. Pausing or hiding animation does not "
         "remove its saved tracks or keys."},
        {"control/Animation tracks", "Animation tracks",
         "Inspect tracks bound to this material. They can change UVs, colors or textures during "
         "playback and override base values."},
        {"control/Appearance colors", "Appearance colors",
         "Choose character appearance colors for the assembled clothing/model preview."},
        {"control/Apply Pokemon table", "Apply Pokemon table",
         "Apply the edited encounter slots to the selected table. Shared table edits can affect "
         "more than one region."},
        {"control/Apply UV transform", "Apply UV transform",
         "Apply the entered transform to selected UV coordinates. Check seams and the texture "
         "preview before saving."},
        {"control/Apply and reload map", "Apply and reload map",
         "Save and stage the reviewed overworld placement edits, then reopen the map using staged "
         "assets. Build game export separately when you want an export folder."},
        {"control/Apply attribute", "Apply attribute",
         "Assign the selected collision attribute to the selected triangles. Attributes affect "
         "movement behavior, not just preview colors."},
        {"control/Apply bind transform", "Apply bind transform",
         "Apply the edited bone bind transform to the skeleton. Check skinned geometry and related "
         "motions after saving."},
        {"control/Apply grid", "Apply grid",
         "Apply the entered grid parameters to the authored surface layout. Review existing cells "
         "and placements before changing the grid."},
        {"control/Apply ground types and table", "Apply ground types and table",
         "Apply the selected surface-type filters and encounter-table reference to the region."},
        {"control/Apply height", "Apply height",
         "Change the selected surface's height by the entered value or increment. Check adjacent "
         "boundaries and player collision after editing."},
        {"control/Apply replacement", "Apply replacement",
         "Apply the reviewed audio conversion/replacement to the selected sample. Listen to the "
         "edited clip before saving."},
        {"control/Apply simplification", "Apply simplification",
         "Apply the reviewed simplification to the selected authored terrain. Check silhouette and "
         "surface detail before saving."},
        {"control/Apply texture", "Apply texture",
         "Encode the imported PNG in the chosen output format and add or replace the texture. "
         "Replacing a texture keeps its stored mip-level count and, by default, its format."},
        {"control/Apply texture size", "Apply texture size",
         "Apply the configured texture scale to the selected authored surface so tiling matches "
         "the intended world size."},
        {"control/Apply transform", "Apply transform",
         "Apply the entered translation, rotation or scale to selected editable geometry. Check "
         "the pivot and affected selection first."},
        {"control/Approximate collision", "Approximate collision",
         "Create or refit an approximate collision box around the selected model. Inspect the "
         "approximation; detailed geometry may need custom collision."},
        {"control/Arena and trainer", "Arena and trainer",
         "Choose arena and trainer context for battle preview. This does not change battle "
         "encounters or trainer teams."},
        {"control/Assembled outfit", "Assembled outfit",
         "Preview clothing as an assembled character outfit instead of isolated parts."},
        {"control/Assign selected faces", "Assign selected faces",
         "Assign the chosen material to selected faces. This changes the model's geometry/material "
         "binding."},
        {"control/At view center", "At view center",
         "Place the 3D cursor at the viewport's current target. Use a placement's cursor action to "
         "move that placement here."},
        {"control/Attach 100%", "Attach 100%",
         "Attach the selected vertices entirely to the chosen bone. This replaces their previous "
         "distribution of influences."},
        {"control/Audio", "Audio",
         "Preview and replace music, sounds and cries. Listen to converted replacements before "
         "saving and staging."},
        {"control/Authoring", "Authoring",
         "Build map compositions, extract reusable objects, shape ground and place assets. These "
         "are authored project assets that need compilation."},
        {"control/Automatically stage saved changes", "Automatically stage saved changes",
         "Periodically compile saved edits when the editor is ready. This can take time; built "
         "game exports remain a separate action."},
        {"control/Autosave", "Autosave",
         "Save editable project documents periodically. Autosave does not by itself compile them "
         "into staged assets or build an export."},
        {"control/Background", "Background",
         "Choose a preview lighting/background preset to inspect the model. This does not edit the "
         "game's environment lighting."},
        {"control/Battle arena preview", "Battle arena preview",
         "Preview the model with arena, trainer and battle framing settings. This is not a "
         "simulation of an actual battle."},
        {"control/Blend a second texture", "Blend a second texture",
         "Configure a second surface texture and its blend to create transitions such as grass "
         "meeting sand."},
        {"control/Blend selected cells", "Blend selected cells",
         "Apply the configured blend values to the selected cells, mixing the base and second "
         "texture layers."},
        {"control/Blender", "Blender",
         "Export a supported model or motion for Blender, then import compatible edits back into "
         "Studio. Keep the exchange metadata and matching source document."},
        {"control/Blender object exchange", "Blender object exchange",
         "Export an authored/extracted object for Blender, then import compatible edits. Source "
         "material assignments and exchange metadata must be preserved."},
        {"control/Bloom", "Bloom",
         "Toggle this rendering feature in the preview. Compare the appearance without changing "
         "the saved game material or environment."},
        {"control/Bones", "Bones",
         "Inspect the skeleton's bone hierarchy and influences. Use the Skeleton editor for "
         "supported bind-pose edits."},
        {"control/Borrow effect into selected material", "Borrow effect into selected material",
         "Apply the selected donor configuration to the current material. Review the result and "
         "texture assignments before saving."},
        {"control/Borrow fragment shader", "Borrow fragment shader",
         "Choose a compatible donor's combiner configuration for the selected material. Review "
         "texture inputs after borrowing it."},
        {"control/Borrow into selected material", "Borrow into selected material",
         "Apply the selected donor configuration to the current material. Review the result and "
         "texture assignments before saving."},
        {"control/Borrow material effect", "Borrow material effect",
         "Choose a donor material effect and decide which material properties to transfer."},
        {"control/Bottom center", "Bottom center",
         "Set the extracted object's pivot to its bottom center or geometric center. The pivot "
         "affects placement, rotation and alignment."},
        {"control/Box", "Box",
         "Use rectangle selection in the viewport. Check X-ray selection if you also want elements "
         "behind visible surfaces."},
        {"control/Box (B)", "Box (B)",
         "Use rectangle selection in the viewport. Check X-ray selection if you also want elements "
         "behind visible surfaces."},
        {"control/Browse", "Browse",
         "Choose a folder or file for the adjacent field. Check the field label to distinguish the "
         "project location from the original dump or an export destination."},
        {"control/Browse character models", "Browse character models",
         "Load the character catalog and choose a compatible model for this placement. Wait for "
         "loading to finish before selecting a resource."},
        {"control/Buffer writes", "Buffer writes",
         "Control which stage outputs are stored for later texture-combiner stages. This changes "
         "the material's color/alpha calculation."},
        {"control/Build game export", "Build game export",
         "Write the currently staged resources to the export folder. Save and stage new edits "
         "first: Build does not automatically do those steps."},
        {"control/Camera pose guides", "Camera pose guides",
         "Configure spatial overlay rendering and picking. Enable only the layers needed for the "
         "task to make selection clearer."},
        {"control/Camera settings", "Camera settings",
         "Inspect camera region properties and preview helpers. Supported camera-data edits are "
         "made in the Cameras workspace."},
        {"control/Cameras", "Cameras",
         "Inspect camera regions and edit camera behavior for a map. A free viewport camera is "
         "separate from saved game camera data."},
        {"control/Cancel", "Cancel",
         "Cancel the current operation or preview. Read any pending-changes dialog before "
         "discarding edits."},
        {"control/Cancel edit", "Cancel edit",
         "Cancel the current operation or preview. Read any pending-changes dialog before "
         "discarding edits."},
        {"control/Cancel load", "Cancel load",
         "Cancel the ongoing asset load. Wait for cancellation before starting another load."},
        {"control/Cancel loading", "Cancel loading",
         "Cancel the ongoing asset load. Wait for cancellation before starting another load."},
        {"control/Cancel preview", "Cancel preview",
         "Cancel the current operation or preview. Read any pending-changes dialog before "
         "discarding edits."},
        {"control/Cancel simplification", "Cancel simplification",
         "Cancel the current operation or preview. Read any pending-changes dialog before "
         "discarding edits."},
        {"control/Capture current view", "Capture current view",
         "Copy the current viewing pose into the selected camera's editable settings. Review and "
         "save the resulting camera edit."},
        {"control/Capture view", "Capture view",
         "Copy the current camera pose into the selected camera edit fields. Review and save "
         "before exporting."},
        {"control/Center", "Center",
         "Set the extracted object's pivot to its bottom center or geometric center. The pivot "
         "affects placement, rotation and alignment."},
        {"control/Changes, history and settings...", "Changes, history and settings...",
         "Inspect saved edits, staged resources and project history, and configure autosave and "
         "staging preferences."},
        {"control/Characters", "Characters",
         "Control which character placements are visible. Story-dependent placements may overlap "
         "because the editor is not simulating story progression."},
        {"control/Check export", "Check export",
         "Validate the authored composition and its export constraints before writing game "
         "resources. Read any reported problems before building."},
        {"control/Check return link", "Check return link",
         "Inspect whether the destination provides a matching route back. A valid outgoing "
         "destination does not guarantee a return entrance."},
        {"control/Checkerboard", "Checkerboard",
         "Show pixel boundaries or a checkerboard behind transparent pixels to inspect the image. "
         "These are display aids."},
        {"control/Choose GARC file...", "Choose GARC file...",
         "Choose an external archive for this operation. Use an archive compatible with the "
         "selected resource and game."},
        {"control/Choose another map in Maps", "Choose another map in Maps",
         "Return to the indicated workspace to continue authoring or select a different map. "
         "Respond to any pending-edit prompt before switching."},
        {"control/Choose character...", "Choose character...",
         "Load or browse available character resources for this placement. Resource choice is "
         "separate from its event/script behavior."},
        {"control/Choose layers", "Choose layers",
         "Enable interaction overlays such as entrances, NPCs, pickups and encounters. Layers can "
         "be visible without being selectable when locked."},
        {"control/Choose map in Maps", "Choose map in Maps",
         "Return to Maps to load the area needed by this editor. Wait for loading to finish before "
         "selecting regions."},
        {"control/Choose override folder...", "Choose override folder...",
         "Select the folder that will receive exported replacement resources."},
        {"control/Choose surface...", "Choose surface...",
         "Choose a surface or texture asset for the selected authored ground. Preview it before "
         "painting or replacing the current assignment."},
        {"control/Choose texture...", "Choose texture...",
         "Choose a surface or texture asset for the selected authored ground. Preview it before "
         "painting or replacing the current assignment."},
        {"control/Clear", "Clear",
         "Clear the current selection or region filter. This does not delete the underlying model "
         "or placement."},
        {"control/Clear category highlight", "Clear category highlight",
         "Remove the temporary reaction-category highlight. The mask data is unchanged."},
        {"control/Clear channel", "Clear channel",
         "Remove the edited channel's keys so its fallback/base value is used. Check the animation "
         "after clearing it."},
        {"control/Clear measurement", "Clear measurement",
         "Clear the ruler measurement or exchange its endpoints. Map geometry is unchanged."},
        {"control/Clear region", "Clear region",
         "Clear the current selection or region filter. This does not delete the underlying model "
         "or placement."},
        {"control/Clear selection", "Clear selection",
         "Clear the current selection or region filter. This does not delete the underlying model "
         "or placement."},
        {"control/Clear simulated values", "Clear simulated values",
         "Set or clear simulated runtime values used by the camera preview. These help inspect "
         "conditions without editing a game save."},
        {"control/Clothing source", "Clothing source",
         "Inspect the source archive selection for this viewer. In project mode sources are "
         "managed by the project."},
        {"control/Collision", "Collision",
         "Inspect and edit the invisible surfaces and boundaries used for movement. Visible "
         "scenery and collision are separate resources."},
        {"control/Collision attribute colors", "Collision attribute colors",
         "Configure spatial overlay rendering and picking. Enable only the layers needed for the "
         "task to make selection clearer."},
        {"control/Color by attribute", "Color by attribute",
         "Choose how collision geometry is displayed. Attribute colors help identify different "
         "movement surfaces and boundaries."},
        {"control/Colors", "Colors",
         "Edit material colors and texture-stage constants. Animated color tracks can override a "
         "constant during playback; inspect Motions when a change seems hidden."},
        {"control/Conditional camera replacements", "Conditional camera replacements",
         "Inspect camera alternatives selected by runtime conditions. Preview simulation does not "
         "change game save values."},
        {"control/Connected", "Connected",
         "Select connected geometry or UV islands. Separate shells and UV seams can divide a model "
         "into several groups."},
        {"control/Continue painting", "Continue painting",
         "Return to ground editing after reviewing export or generation settings."},
        {"control/Continue shaping", "Continue shaping",
         "Return to ground editing after reviewing export or generation settings."},
        {"control/Controls", "Controls",
         "Show the input reference for this viewport. Controls vary between free navigation, "
         "player preview and model editing."},
        {"control/Copy XYZ", "Copy XYZ",
         "Copy the displayed values or report to the clipboard. Review copied reports for local "
         "paths before sharing them."},
        {"control/Copy details", "Copy details",
         "Copy the displayed values or report to the clipboard. Review copied reports for local "
         "paths before sharing them."},
        {"control/Copy disassembly", "Copy disassembly",
         "Copy the displayed values or report to the clipboard. Review copied reports for local "
         "paths before sharing them."},
        {"control/Copy measurement", "Copy measurement",
         "Copy the displayed values or report to the clipboard. Review copied reports for local "
         "paths before sharing them."},
        {"control/Copy selected material", "Copy selected material",
         "Create a material copy so faces can use independently edited settings. Assign the "
         "intended faces to the new material."},
        {"control/Create Project", "Create Project",
         "Create a separate project folder for edits using an extracted game dump containing "
         "romfs. Keep the original dump as the source; choose an empty project directory."},
        {"control/Create donor bundle...", "Create donor bundle...",
         "Create the new Pokemon bundle from the chosen donor assets and reviewed entry settings. "
         "This adds project resources; inspect the new entry afterwards."},
        {"control/Create flat ground", "Create flat ground",
         "Create a flat authored ground surface using the configured dimensions. Replacing "
         "existing authored ground changes the composition; inspect the selection first."},
        {"control/Create map from template...", "Create map from template...",
         "Create a map from an existing map's resources and behavior. Review inherited scripts, "
         "encounters and entrances before using the new map in game."},
        {"control/Create new", "Create new",
         "Create a separate project folder for edits using an extracted game dump containing "
         "romfs. Keep the original dump as the source; choose an empty project directory."},
        {"control/Create, stage and open map", "Create, stage and open map",
         "Create the reviewed map, compile it into the project and reopen it for editing. This "
         "changes project resources; review the plan first."},
        {"control/Cries", "Cries",
         "Preview and replace Pokemon cry audio. Imported sounds need conversion and auditioning "
         "before saving."},
        {"control/Customize collision", "Customize collision",
         "Open collision editing for the authored map. Custom collision is separate from the "
         "visible terrain and needs validation after geometry changes."},
        {"control/Cutaway", "Cutaway",
         "Enable the authoring viewport's cutaway display to inspect obscured geometry. This does "
         "not delete mesh faces."},
        {"control/Dark", "Dark",
         "Choose a preview lighting/background preset to inspect the model. This does not edit the "
         "game's environment lighting."},
        {"control/Day music", "Day music",
         "Choose which of the zone's authored music tracks to preview. This does not change the "
         "map's music assignment."},
        {"control/Delete key", "Delete key",
         "Remove the selected frame's key from the current channel. Interpolation between the "
         "remaining keys may change."},
        {"control/Delete placement", "Delete placement",
         "Remove the selected authored placement from the composition. The reusable asset remains "
         "in the asset library."},
        {"control/Delete region", "Delete region",
         "Remove the selected encounter region. Check surrounding encounter coverage before "
         "saving."},
        {"control/Delete selected", "Delete selected",
         "Delete the selected elements in this editor. Check the active selection and review any "
         "confirmation before applying."},
        {"control/Discard", "Discard",
         "Discard the pending changes covered by this dialog. Choose Cancel if you want to return "
         "and save them first."},
        {"control/Discard and continue", "Discard and continue",
         "Discard the pending changes covered by this dialog. Choose Cancel if you want to return "
         "and save them first."},
        {"control/Discard cry changes", "Discard cry changes",
         "Discard unsaved edits in this editor and continue with the requested operation."},
        {"control/Discard image changes", "Discard image changes",
         "Discard unsaved edits in this editor and continue with the requested operation."},
        {"control/Discard settings", "Discard settings",
         "Discard unsaved edits in this editor and continue with the requested operation."},
        {"control/Discard unsaved palette changes", "Discard unsaved palette changes",
         "Discard the current unsaved palette edits before switching sources."},
        {"control/Display & selection", "Display & selection",
         "Configure how region overlays are drawn and picked. These options affect inspection, not "
         "the game data."},
        {"control/Document...", "Document...",
         "Open the material/model document actions. Project saving, standalone documents and "
         "game-resource exports are distinct operations."},
        {"control/Duplicate one tile over", "Duplicate one tile over",
         "Create a copy of the selected placement offset by one tile. Review the new position "
         "before saving."},
        {"control/Duplicate region", "Duplicate region",
         "Create a copy of the selected encounter region. Review its shape and shared table before "
         "assigning different Pokemon."},
        {"control/Edge (2)", "Edge (2)",
         "Select mesh edges. Edge selection changes which vertices participate in geometry "
         "editing."},
        {"control/Edit UVs", "Edit UVs",
         "Enable UV editing for the selected texture/material context. UV changes affect how the "
         "texture maps to the mesh."},
        {"control/Edit collision", "Edit collision",
         "Open collision editing for the authored map. Custom collision is separate from the "
         "visible terrain and needs validation after geometry changes."},
        {"control/Edit color palettes", "Edit color palettes",
         "Edit the clothing palette resource. Palette edits can affect other clothing using the "
         "same colors."},
        {"control/Edit in bind pose", "Edit in bind pose",
         "Show the model in its unanimated bind pose while editing geometry. This makes vertex and "
         "bone relationships easier to inspect."},
        {"control/Edit project asset", "Edit project asset",
         "Edit a reusable project asset or extract selected source geometry as a new asset. Trim "
         "the geometry and set its pivot before saving to the asset library."},
        {"control/Edit selected", "Edit selected",
         "Open the selected overworld placement's editable fields. Inspect the target record "
         "before changing values."},
        {"control/Edit selected pickup", "Edit selected pickup",
         "Edit the selected item pickup's supported fields. Check item identity, placement and "
         "collection flag relationships."},
        {"control/Edit weather schedule", "Edit weather schedule",
         "Choose weather for morning, daytime, evening, night and midnight. These are saved zone "
         "edits. Game saves and scripts may override the schedule during gameplay."},
        {"control/Enter player mode", "Enter player mode",
         "Switch between walking preview and the free viewing camera. Player preview checks "
         "movement and collision but does not execute story scripts."},
        {"control/Entrances...", "Entrances...",
         "Open entrance/warp editing for the selected map. Source trigger and destination arrival "
         "data are distinct."},
        {"control/Environment", "Environment",
         "Preview the selected zone's lights, time, sky and weather. Weather schedule editing is a "
         "saved change; ordinary lighting and time controls are preview settings."},
        {"control/Erase / background", "Erase / background",
         "Paint the background/unassigned category into the interaction mask rather than a "
         "reaction region."},
        {"control/Exit cameras", "Exit cameras",
         "Leave the specialized feeding/camera preview and return to the regular model view."},
        {"control/Exit feeding", "Exit feeding",
         "Leave the specialized feeding/camera preview and return to the regular model view."},
        {"control/Export PNG...", "Export PNG...",
         "Write the current image as a PNG for external editing. The game's texture remains "
         "unchanged."},
        {"control/Export Pokemon GARC...", "Export Pokemon GARC...",
         "Export this editor's changes as game resources to the chosen destination. In project "
         "workflows, Save Project, Stage Project and Build game export provide the combined "
         "export."},
        {"control/Export WAV...", "Export WAV...",
         "Write the selected audio to a WAV file for listening or external editing. Exporting a "
         "WAV does not replace the game's sound."},
        {"control/Export audio override...", "Export audio override...",
         "Export the edited audio resources to a separate override destination. Project Build game "
         "export combines staged edits across resource types."},
        {"control/Export camera GARC...", "Export camera GARC...",
         "Export this editor's changes as game resources to the chosen destination. In project "
         "workflows, Save Project, Stage Project and Build game export provide the combined "
         "export."},
        {"control/Export collision OBJ...", "Export collision OBJ...",
         "Export collision geometry to OBJ for external editing. Include reference scenery only "
         "when useful, and keep it distinct from collision meshes."},
        {"control/Export cry override...", "Export cry override...",
         "Export this editor's changes as game resources to the chosen destination. In project "
         "workflows, Save Project, Stage Project and Build game export provide the combined "
         "export."},
        {"control/Export current cry WAV...", "Export current cry WAV...",
         "Write the selected audio to a WAV file for listening or external editing. Exporting a "
         "WAV does not replace the game's sound."},
        {"control/Export for Blender", "Export for Blender",
         "Export the current supported model for the USUMStudio Blender exchange workflow. "
         "Preserve the generated metadata and use the matching add-on."},
        {"control/Export game images...", "Export game images...",
         "Export this editor's changes as game resources to the chosen destination. In project "
         "workflows, Save Project, Stage Project and Build game export provide the combined "
         "export."},
        {"control/Export initial bundle GARC...", "Export initial bundle GARC...",
         "Export the newly assembled Pokemon bundle archive. Further Studio edits need their own "
         "save/stage/export cycle."},
        {"control/Export object...", "Export object...",
         "Export this object and its preview textures for Blender editing using the USUMStudio "
         "exchange workflow."},
        {"control/Export objects", "Export objects",
         "Include this authored resource category in the export. Review scope so intended changes "
         "are compiled."},
        {"control/Export override folder", "Export override folder",
         "Write modified game resources into a separate output folder. Use this to keep an "
         "original dump unchanged."},
        {"control/Export override...", "Export override...",
         "Export this editor's changes as game resources to the chosen destination. In project "
         "workflows, Save Project, Stage Project and Build game export provide the combined "
         "export."},
        {"control/Export placement override...", "Export placement override...",
         "Export this editor's changes as game resources to the chosen destination. In project "
         "workflows, Save Project, Stage Project and Build game export provide the combined "
         "export."},
        {"control/Export selected motion", "Export selected motion",
         "Export only the selected animation for Blender editing. Keep it associated with the "
         "matching model and skeleton."},
        {"control/Export terrain", "Export terrain",
         "Include this authored resource category in the export. Review scope so intended changes "
         "are compiled."},
        {"control/External GARC...", "External GARC...",
         "Choose an external archive for this operation. Use an archive compatible with the "
         "selected resource and game."},
        {"control/Extract asset", "Extract asset",
         "Edit a reusable project asset or extract selected source geometry as a new asset. Trim "
         "the geometry and set its pivot before saving to the asset library."},
        {"control/Extrude boundary edge", "Extrude boundary edge",
         "Extend the selected terrain boundary with additional geometry. Inspect the new edge and "
         "generated collision afterwards."},
        {"control/Face", "Face",
         "Select triangles/faces rather than individual vertices. Face selection is useful for "
         "material assignment, extraction and collision editing."},
        {"control/Face (3)", "Face (3)",
         "Select triangles/faces rather than individual vertices. Face selection is useful for "
         "material assignment, extraction and collision editing."},
        {"control/Far side", "Far side",
         "Preview the Pokemon on the opposite battle side to inspect framing and orientation."},
        {"control/Feeding", "Feeding",
         "Preview feeding interactions and edit supported saved feeding parameters. Feeding-area "
         "bounds affect only the preview; the game's feeding rectangle is fixed."},
        {"control/Feeding area bounds", "Feeding area bounds",
         "Adjust the feeding rectangle in the preview. These bounds are not saved to game assets "
         "or included in exports."},
        {"control/Feeding camera (saved)", "Feeding camera (saved)",
         "Edit the supported saved feeding-camera parameters. These are different from simply "
         "moving the preview camera."},
        {"control/Filled collision", "Filled collision",
         "Choose how collision geometry is displayed. Attribute colors help identify different "
         "movement surfaces and boundaries."},
        {"control/Filled regions", "Filled regions",
         "Configure spatial overlay rendering and picking. Enable only the layers needed for the "
         "task to make selection clearer."},
        {"control/Fit UVs", "Fit UVs",
         "Frame the visible UV coordinates in the UV editor. This changes the view, not the UV "
         "data."},
        {"control/Fit environment", "Fit environment",
         "Fit the viewing camera around the map. Large scenery and sky geometry can make the "
         "playable area look small; Start at map spawn returns to a useful starting location."},
        {"control/Fit map", "Fit map",
         "Fit the viewing camera around the map. Large scenery and sky geometry can make the "
         "playable area look small; Start at map spawn returns to a useful starting location."},
        {"control/Flatten mesh", "Flatten mesh",
         "Set selected terrain heights to the chosen level. Adjacent unselected cells may create "
         "steep transitions."},
        {"control/Flatten selected cells", "Flatten selected cells",
         "Set selected terrain heights to the chosen level. Adjacent unselected cells may create "
         "steep transitions."},
        {"control/Flatten selection", "Flatten selection",
         "Flatten the selected geometry along the chosen axis/plane. This changes vertex "
         "positions."},
        {"control/Fog", "Fog",
         "Toggle this rendering feature in the preview. Compare the appearance without changing "
         "the saved game material or environment."},
        {"control/Follow active camera", "Follow active camera",
         "Use the active or explicitly selected game camera for preview. Editing the free viewing "
         "camera alone does not change the game camera settings."},
        {"control/Fragment lighting", "Fragment lighting",
         "Enable the material's fragment lighting. This is a material edit, unlike the viewport's "
         "general lighting preview switch."},
        {"control/Frame", "Frame",
         "Move and zoom the viewing camera to the selected subject. The subject itself stays in "
         "place."},
        {"control/Frame arrival", "Frame arrival",
         "Move the viewing camera to the entrance's arrival position. This does not change the "
         "entrance coordinates."},
        {"control/Frame cursor", "Frame cursor",
         "Move and zoom the viewing camera to the selected subject. The subject itself stays in "
         "place."},
        {"control/Frame map", "Frame map",
         "Fit the viewing camera around the map. Large scenery and sky geometry can make the "
         "playable area look small; Start at map spawn returns to a useful starting location."},
        {"control/Frame mesh", "Frame mesh",
         "Move and zoom the viewing camera to the selected subject. The subject itself stays in "
         "place."},
        {"control/Frame model", "Frame model",
         "Move and zoom the viewing camera to the selected subject. The subject itself stays in "
         "place."},
        {"control/Frame object (F)", "Frame object (F)",
         "Move and zoom the viewing camera to the selected subject. The subject itself stays in "
         "place."},
        {"control/Frame pacing", "Frame pacing",
         "Control preview frame rate and background refresh. These are editor performance "
         "preferences, not game timing changes."},
        {"control/Frame player", "Frame player",
         "Move the viewing camera to the preview player's position. The player's map data is "
         "unchanged."},
        {"control/Frame region", "Frame region",
         "Move and zoom the viewing camera to the selected subject. The subject itself stays in "
         "place."},
        {"control/Frame selection", "Frame selection",
         "Move and zoom the viewing camera to the selected subject. The subject itself stays in "
         "place."},
        {"control/Free camera", "Free camera",
         "Leave player/camera-follow preview and return to free viewport navigation."},
        {"control/Game indoor light", "Game indoor light",
         "Choose a preview lighting preset to inspect the model under different illumination."},
        {"control/Geometry", "Geometry",
         "Edit the mesh's vertices, faces, material assignment and skin weights. Use selection "
         "tools to limit the edit before transforming geometry."},
        {"control/Gray", "Gray",
         "Choose a preview lighting/background preset to inspect the model. This does not edit the "
         "game's environment lighting."},
        {"control/Grid guide", "Grid guide",
         "Show a grid in the authoring viewport to help judge spacing and scale. This is a preview "
         "overlay."},
        {"control/Ground types and table", "Ground types and table",
         "Choose which ground attributes the region applies to and which encounter table it "
         "references."},
        {"control/Hide all", "Hide all",
         "Hide or deselect all items in this panel. This is a viewing/selection action, not "
         "deletion from the game."},
        {"control/Hide range", "Hide range",
         "Set whether the selected mesh is visible over the entered frame range. This edits "
         "visibility animation, not just the inspection view."},
        {"control/High sun", "High sun",
         "Choose a preview lighting preset to inspect the model under different illumination."},
        {"control/Highlight assigned faces", "Highlight assigned faces",
         "Highlight faces that use the chosen material without changing their assignment."},
        {"control/History", "History",
         "Review saved project revisions and restore an earlier state when needed. Read the "
         "selected revision before restoring it."},
        {"control/I have reserved this flag for this pickup",
         "I have reserved this flag for this pickup",
         "Confirm that the selected pickup flag is reserved for this item. Reusing a collection "
         "flag can make different pickups share collection state."},
        {"control/ID edges", "ID edges",
         "Show outline-related rendering in the preview to inspect the current settings."},
        {"control/Idle variations", "Idle variations",
         "Inspect/edit the supported idle-motion variation settings for the Pokemon."},
        {"control/Images", "Images",
         "Browse game images, export PNGs and replace selected resources. Check dimensions and "
         "preview before saving."},
        {"control/Import BCSTM...", "Import BCSTM...",
         "Replace the selected music stream with a compatible BCSTM file. Check audio and loop "
         "points before saving and staging."},
        {"control/Import WAV...", "Import WAV...",
         "Choose a replacement WAV, then inspect its conversion settings and preview it before "
         "applying the replacement."},
        {"control/Import edited OBJ...", "Import edited OBJ...",
         "Import edited collision geometry from OBJ. Check mesh categories and attributes, then "
         "validate movement boundaries before export."},
        {"control/Import edited object...", "Import edited object...",
         "Replace the current object geometry with a compatible Blender exchange file. Review "
         "pivot, UVs and material assignments before saving."},
        {"control/Import from Blender", "Import from Blender",
         "Import compatible edits exported by the USUMStudio Blender add-on. Review geometry, "
         "materials and skeleton compatibility before saving and staging."},
        {"control/Import music...", "Import music...",
         "Choose replacement music, inspect conversion and loop settings, and preview the result "
         "before saving it to the project."},
        {"control/Import selected motion", "Import selected motion",
         "Import the edited animation into the selected motion. Confirm the motion target and "
         "skeleton before saving."},
        {"control/Include colors and render state", "Include colors and render state",
         "Include donor colors and render settings with the borrowed effect. Leave this off when "
         "you want to preserve those properties."},
        {"control/Include map reference geometry", "Include map reference geometry",
         "Include map geometry as a reference in the exported collision OBJ. Keep reference meshes "
         "separate from editable collision when importing."},
        {"control/Include overlays in Ctrl+click selection",
         "Include overlays in Ctrl+click selection",
         "Configure spatial overlay rendering and picking. Enable only the layers needed for the "
         "task to make selection clearer."},
        {"control/Include story-dependent characters", "Include story-dependent characters",
         "Control which character placements are visible. Story-dependent placements may overlap "
         "because the editor is not simulating story progression."},
        {"control/Initial buffer color", "Initial buffer color",
         "Set the initial combiner buffer color used before a stage writes a replacement value."},
        {"control/Inspect asset", "Inspect asset",
         "Inspect the selected library asset independently of its placements in the map."},
        {"control/Inspect destination entrance", "Inspect destination entrance",
         "Select or inspect the arrival record referenced by this entrance so you can verify the "
         "connection."},
        {"control/Inspect interaction", "Inspect interaction",
         "Read the selected NPC, trigger or scenery interaction's resolved script actions. Script "
         "inspection is read-only; unresolved calls may still require manual analysis."},
        {"control/Islands", "Islands",
         "Select connected geometry or UV islands. Separate shells and UV seams can divide a model "
         "into several groups."},
        {"control/Isolate", "Isolate",
         "Hide other mesh parts in the preview to focus on the enabled selection. This does not "
         "delete those parts."},
        {"control/Isolate enabled", "Isolate enabled",
         "Hide other mesh parts in the preview to focus on the enabled selection. This does not "
         "delete those parts."},
        {"control/Keep original pass", "Keep original pass",
         "Keep the original material rendering pass alongside the configured effect. Compare the "
         "result before saving."},
        {"control/Keep region boundary", "Keep region boundary",
         "Constrain terrain processing to the chosen region or preserve its boundary. Check the "
         "preview before applying a reduction or shape change."},
        {"control/Keep seams together", "Keep seams together",
         "Move matching seam vertices together where supported to avoid splitting the surface "
         "during an edit."},
        {"control/Keep selected", "Keep selected",
         "Keep selected faces and remove the rest from the extracted object. This edits the "
         "extraction, not the original source map."},
        {"control/Keep shared vertices together", "Keep shared vertices together",
         "Move connected/shared collision vertices together to avoid opening gaps during editing."},
        {"control/Keep the template's existing behavior", "Keep the template's existing behavior",
         "Acknowledge that the new map inherits the template's gameplay data. Geometry replacement "
         "alone does not rewrite scripts or story behavior."},
        {"control/Light", "Light",
         "Choose a preview lighting/background preset to inspect the model. This does not edit the "
         "game's environment lighting."},
        {"control/Light follows camera", "Light follows camera",
         "Toggle this rendering feature in the preview. Compare the appearance without changing "
         "the saved game material or environment."},
        {"control/Lighting", "Lighting",
         "Adjust this model's preview lighting to inspect shape and materials. Saved material "
         "lighting settings are edited in Studio materials."},
        {"control/Limit FPS", "Limit FPS",
         "Reduce rendering work through a frame cap or slower background refresh. This affects "
         "editor responsiveness and resource use."},
        {"control/Limit brush to selection", "Limit brush to selection",
         "Restrict weight painting to selected vertices instead of every vertex under the brush."},
        {"control/Limit to selected region", "Limit to selected region",
         "Constrain terrain processing to the chosen region or preserve its boundary. Check the "
         "preview before applying a reduction or shape change."},
        {"control/Load audio library", "Load audio library",
         "Read the project's audio catalog so music, effects and cries can be browsed. Reload "
         "staged assets first when you need newly compiled source changes."},
        {"control/Load character catalog", "Load character catalog",
         "Load or browse available character resources for this placement. Resource choice is "
         "separate from its event/script behavior."},
        {"control/Load cry source", "Load cry source",
         "Load the selected Pokemon cry resources so the original and edited audio can be "
         "previewed."},
        {"control/Load destination list", "Load destination list",
         "Read available entrance destinations from the current project source. Choose the "
         "intended zone and entrance, then validate the connection."},
        {"control/Load map", "Load map",
         "Load the selected map from the project's current source. Selecting a name alone does not "
         "load it. Wait for loading to finish; Start at map spawn controls the initial camera "
         "position."},
        {"control/Loaded archive sources", "Loaded archive sources",
         "Inspect which source resources supplied the currently loaded map. This diagnostic view "
         "does not change archive assignments."},
        {"control/Loading character catalog...", "Loading character catalog...",
         "Load the character catalog and choose a compatible model for this placement. Wait for "
         "loading to finish before selecting a resource."},
        {"control/Lock", "Lock",
         "Prevent this overlay layer from being selected while keeping it visible. Unlock it "
         "before trying to pick its regions."},
        {"control/Loop", "Loop",
         "Repeat preview playback. Use track loop follows the audio file's authored loop points "
         "when available."},
        {"control/Looping effects", "Looping effects",
         "Repeat effect playback in the preview. The underlying game motion remains unchanged."},
        {"control/Lower", "Lower",
         "Change the selected surface's height by the entered value or increment. Check adjacent "
         "boundaries and player collision after editing."},
        {"control/Main model", "Main model",
         "Choose which resource to inspect. The main model and its separate shadow can have "
         "different geometry and materials."},
        {"control/Make table unique", "Make table unique",
         "Create an independent encounter table for this region before changing its Pokemon. Other "
         "regions using the original table retain that table."},
        {"control/Map", "Map",
         "Work with the loaded map/composition. Other views can focus on individual reusable "
         "assets."},
        {"control/Map diagnostics", "Map diagnostics",
         "Read loading and rendering diagnostics for the current map. Review local paths before "
         "sharing a copied report."},
        {"control/Map music", "Map music",
         "Choose a loaded zone and its day/night track to preview map music. These controls do not "
         "edit the zone's track assignment."},
        {"control/Map selection", "Map selection",
         "Choose a named map and zone, then press Load map. The dropdown selection and the map "
         "already visible in the viewport can differ until loading completes."},
        {"control/Maps", "Maps",
         "Browse maps, inspect surfaces and placements, and adjust the environment preview. Start "
         "with a named map and Load map."},
        {"control/Mark measurement start", "Mark measurement start",
         "Set the ruler's first endpoint at the current 3D cursor. Move the cursor to measure a "
         "distance."},
        {"control/Material coverage", "Material coverage",
         "Inspect whether the selected material's inputs are supported and present. A preview "
         "limitation is not necessarily a broken game asset."},
        {"control/Material issues only", "Material issues only",
         "Filter the scene list to materials with reported input or preview issues. This does not "
         "hide or remove saved materials."},
        {"control/Material state", "Material state",
         "Inspect the selected material's render settings. Use Studio for supported edits."},
        {"control/Materials", "Materials",
         "Choose material assignment editing or material inspection for the current model. Faces "
         "can share one material across many parts."},
        {"control/Measure project storage", "Measure project storage",
         "Measure project storage usage to inspect how much space source data, revisions and "
         "compiled resources occupy."},
        {"control/Mesh and export notes", "Mesh and export notes",
         "Read source/geometry details and validation notes relevant to compiling the authored "
         "ground."},
        {"control/Mesh sections", "Mesh sections",
         "Select source mesh sections for extraction. Ctrl adds to the selection; verify the "
         "highlighted faces before keeping or deleting them."},
        {"control/Mesh visibility animation", "Mesh visibility animation",
         "Enable this kind of animation in the viewport. Pausing or hiding animation does not "
         "remove its saved tracks or keys."},
        {"control/Meshes", "Meshes",
         "Inspect individual mesh parts, visibility and material assignments before editing "
         "geometry or resources."},
        {"control/Meshes using this material", "Meshes using this material",
         "Find geometry that shares this material. Editing it can change every listed mesh."},
        {"control/Model bounds", "Model bounds",
         "Inspect/edit model bounding information used by compatible systems. Bounds do not "
         "directly reshape the mesh."},
        {"control/Model resources", "Model resources",
         "Manage the model's material and texture resources. Resource additions/removals can "
         "require face or texture-unit reassignment."},
        {"control/Model-viewer framing", "Model-viewer framing",
         "Adjust preview framing independently of mesh shape and game placement."},
        {"control/Models", "Models",
         "Browse Pokemon, characters and clothing from the project source. Load a model, then send "
         "it to Studio when you want to edit it."},
        {"control/Motions", "Motions",
         "Edit skeletal, material or visibility animation. Select the intended motion and track "
         "before setting or deleting keys."},
        {"control/Move", "Move",
         "Choose translation handles for the current edit selection. Drag a handle or enter "
         "coordinates in the panel; movement edits the selected data."},
        {"control/Move (W)", "Move (W)",
         "Choose translation handles for the current edit selection. Drag a handle or enter "
         "coordinates in the panel; movement edits the selected data."},
        {"control/Move by X / Y / Z", "Move by X / Y / Z",
         "Move selected terrain vertices/cells by the entered offsets. Review the selection and "
         "axis directions first."},
        {"control/Move handles", "Move handles",
         "Choose translation handles for the current edit selection. Drag a handle or enter "
         "coordinates in the panel; movement edits the selected data."},
        {"control/Move selection", "Move selection",
         "Move selected terrain vertices/cells by the entered offsets. Review the selection and "
         "axis directions first."},
        {"control/Move to 3D cursor", "Move to 3D cursor",
         "Use the 3D cursor as the selected placement's position. Check the height and collision "
         "surface before saving the placement."},
        {"control/NPCs and triggers", "NPCs and triggers",
         "Manage overworld placements such as NPCs and story/scenery triggers. Placements "
         "reference shared resources and can have script or event dependencies."},
        {"control/Navigation help", "Navigation help",
         "Show the input reference for this viewport. Controls vary between free navigation, "
         "player preview and model editing."},
        {"control/New Project...", "New Project...",
         "Create a separate project folder for edits using an extracted game dump containing "
         "romfs. Keep the original dump as the source; choose an empty project directory."},
        {"control/New bean", "New bean",
         "Start a new feeding interaction in the preview. This does not add a game item or change "
         "a save file."},
        {"control/New box at cursor", "New box at cursor",
         "Create an encounter region with the chosen shape at the cursor or viewing target. "
         "Configure dimensions, ground filters and encounter table before saving."},
        {"control/New box at view center", "New box at view center",
         "Create an encounter region with the chosen shape at the cursor or viewing target. "
         "Configure dimensions, ground filters and encounter table before saving."},
        {"control/New cylinder at cursor", "New cylinder at cursor",
         "Create an encounter region with the chosen shape at the cursor or viewing target. "
         "Configure dimensions, ground filters and encounter table before saving."},
        {"control/New cylinder at view center", "New cylinder at view center",
         "Create an encounter region with the chosen shape at the cursor or viewing target. "
         "Configure dimensions, ground filters and encounter table before saving."},
        {"control/New species row", "New species row",
         "Allocate a new species-management row for the bundle. Resource support does not "
         "automatically add all gameplay data for a new species."},
        {"control/Night music", "Night music",
         "Choose which of the zone's authored music tracks to preview. This does not change the "
         "map's music assignment."},
        {"control/Night table", "Night table",
         "Choose the night encounter table for editing or inspection. Check the day table "
         "separately."},
        {"control/None", "None",
         "Hide or deselect all items in this panel. This is a viewing/selection action, not "
         "deletion from the game."},
        {"control/Normal maps", "Normal maps",
         "Toggle this rendering feature in the preview. Compare the appearance without changing "
         "the saved game material or environment."},
        {"control/Nudge height", "Nudge height",
         "Adjust the selected collision vertices' height by an increment. Check boundaries with "
         "neighboring triangles."},
        {"control/Numeric transform", "Numeric transform",
         "Enter exact translation, rotation or scale values for the current geometry or UV "
         "selection, then apply the transform."},
        {"control/Objects", "Objects",
         "Browse reusable object assets for placement in the authored composition."},
        {"control/Off", "Off",
         "Disable the current manipulation gizmo. Selection and saved transforms are retained."},
        {"control/Open Project", "Open Project",
         "Open an existing editor project. The editor loads that project's source and saved edit "
         "documents, restarting its workspace when necessary."},
        {"control/Open Project...", "Open Project...",
         "Open an existing editor project. The editor loads that project's source and saved edit "
         "documents, restarting its workspace when necessary."},
        {"control/Open audio project...", "Open audio project...",
         "Open an edit document for this resource type. The editor checks that the document "
         "matches the source assets."},
        {"control/Open composition...", "Open composition...",
         "Open a saved composition document and its authored asset references."},
        {"control/Open cry project...", "Open cry project...",
         "Open an edit document for this resource type. The editor checks that the document "
         "matches the source assets."},
        {"control/Open destination", "Open destination",
         "Load the selected entrance's destination map for inspection. Save pending changes when "
         "prompted."},
        {"control/Open edit document...", "Open edit document...",
         "Load a previously saved edit document for the matching source. Source compatibility is "
         "checked before applying it."},
        {"control/Open existing", "Open existing",
         "Open an existing editor project. The editor loads that project's source and saved edit "
         "documents, restarting its workspace when necessary."},
        {"control/Open exports folder", "Open exports folder",
         "Open the folder containing the built game resources. Rebuild after staging newer changes "
         "before copying files into a game installation."},
        {"control/Open image project...", "Open image project...",
         "Open an edit document for this resource type. The editor checks that the document "
         "matches the source assets."},
        {"control/Open model", "Open model",
         "Load the selected catalog entry into the model viewer. Inspect meshes and motions, then "
         "Send to Studio when you want to edit its resources."},
        {"control/Open palette GARC...", "Open palette GARC...",
         "Load a compatible palette archive for inspection or editing."},
        {"control/Open patch...", "Open patch...",
         "Load a previously saved edit document for the matching source. Source compatibility is "
         "checked before applying it."},
        {"control/Open project folder", "Open project folder",
         "Open the current project's folder in the system file manager. Project documents and "
         "compiled exports have different purposes."},
        {"control/Open settings...", "Open settings...",
         "Open a saved Pokemon-settings document matching the selected source model."},
        {"control/Original", "Original",
         "Show the source image for comparison with the edited image. This does not discard "
         "edits."},
        {"control/Original dump", "Original dump",
         "Choose the extracted game dump containing romfs. The project uses it as the source for "
         "assets; it is not the project folder or export folder."},
        {"control/Outlines", "Outlines",
         "Inspect or configure outline rendering for the selected model/material. Model preview "
         "toggles are separate from editable outline parameters shown here."},
        {"control/Overhead editing view", "Overhead editing view",
         "Use an overhead camera while editing regions and paths. This is a viewing mode."},
        {"control/Paint regions", "Paint regions",
         "Enable editing of the Pokemon interaction-region mask. Choose the intended category and "
         "brush before painting."},
        {"control/Paint selected cells", "Paint selected cells",
         "Assign the chosen surface/texture to the selected ground cells. Save the composition to "
         "retain the paint operation."},
        {"control/Painting help", "Painting help",
         "Show the controls and constraints for painting the interaction mask."},
        {"control/Pause", "Pause",
         "Pause preview playback at its current position. Resume to continue; this does not edit "
         "the animation."},
        {"control/Pause feeding", "Pause feeding",
         "Pause or resume the specialized Pokemon interaction preview without editing its saved "
         "parameters."},
        {"control/Pause player", "Pause player",
         "Pause simulated player movement while inspecting a camera. This is a preview control."},
        {"control/Pause preview", "Pause preview",
         "Pause or resume the specialized Pokemon interaction preview without editing its saved "
         "parameters."},
        {"control/Pause send-out", "Pause send-out",
         "Control the Pokemon send-out preview to inspect model placement and animation. These are "
         "preview actions."},
        {"control/Performance details", "Performance details",
         "Inspect frame timing and resource counts when diagnosing viewport performance."},
        {"control/Pixel grid", "Pixel grid",
         "Show pixel boundaries or a checkerboard behind transparent pixels to inspect the image. "
         "These are display aids."},
        {"control/Place asset", "Place asset",
         "Add a placement of the selected asset to the authored composition. Review its position, "
         "rotation, scale and collision before saving."},
        {"control/Play", "Play",
         "Play the selected sound or animation preview. Original and edited clips let you compare "
         "a replacement before saving."},
        {"control/Play edited clip", "Play edited clip",
         "Play the selected sound or animation preview. Original and edited clips let you compare "
         "a replacement before saving."},
        {"control/Play from map start", "Play from map start",
         "Enter player preview at the map's default start. Use it to inspect scale and collision; "
         "story scripts and full gameplay are not simulated."},
        {"control/Play original clip", "Play original clip",
         "Play the selected sound or animation preview. Original and edited clips let you compare "
         "a replacement before saving."},
        {"control/Play send-out", "Play send-out",
         "Control the Pokemon send-out preview to inspect model placement and animation. These are "
         "preview actions."},
        {"control/Player mode", "Player mode",
         "Walk around the loaded map to inspect scale and collision. This preview does not run the "
         "game's full gameplay, scripts or story state."},
        {"control/Player preview", "Player preview",
         "Enter player preview at the map's default start. Use it to inspect scale and collision; "
         "story scripts and full gameplay are not simulated."},
        {"control/Pokemon", "Pokemon",
         "Inspect/edit the encounter table used by the selected region. Use Make table unique "
         "before making region-specific changes to a shared table."},
        {"control/Pokemon shadow", "Pokemon shadow",
         "Show the Pokemon shadow in preview to inspect its size and placement relative to the "
         "model."},
        {"control/Pokemon source archive", "Pokemon source archive",
         "Inspect the source archive selection for this viewer. In project mode sources are "
         "managed by the project."},
        {"control/Pokemon table", "Pokemon table",
         "Edit the Pokemon slots and supported encounter parameters in the selected table."},
        {"control/Preview coverage", "Preview coverage",
         "Inspect supported model features and known preview limitations before diagnosing a "
         "visual mismatch."},
        {"control/Preview game variables", "Preview game variables",
         "Set simulated runtime inputs for camera inspection. These preview values do not edit a "
         "game save or script."},
        {"control/Preview in source viewer", "Preview in source viewer",
         "Return to the source viewer to inspect this model in context. Use the project "
         "staging/reload workflow for compiled changes."},
        {"control/Preview material motions", "Preview material motions",
         "Enable this kind of animation in the viewport. Pausing or hiding animation does not "
         "remove its saved tracks or keys."},
        {"control/Preview on selected tile", "Preview on selected tile",
         "Temporarily show the chosen surface on the selected tile so you can inspect it before "
         "applying."},
        {"control/Preview outfit", "Preview outfit",
         "Load the assembled outfit or selected clothing part for visual inspection. This does not "
         "replace clothing resources."},
        {"control/Preview outlines", "Preview outlines",
         "Show outline-related rendering in the preview to inspect the current settings."},
        {"control/Preview part", "Preview part",
         "Load the assembled outfit or selected clothing part for visual inspection. This does not "
         "replace clothing resources."},
        {"control/Preview selected camera", "Preview selected camera",
         "Use the active or explicitly selected game camera for preview. Editing the free viewing "
         "camera alone does not change the game camera settings."},
        {"control/Preview send-out", "Preview send-out",
         "Control the Pokemon send-out preview to inspect model placement and animation. These are "
         "preview actions."},
        {"control/Preview simplification", "Preview simplification",
         "Preview a reduction in terrain geometry without committing it. Inspect the result before "
         "Apply simplification."},
        {"control/Preview unsupported inputs", "Preview unsupported inputs",
         "Toggle this rendering feature in the preview. Compare the appearance without changing "
         "the saved game material or environment."},
        {"control/Preview weather", "Preview weather",
         "Choose a temporary weather override for the selected lighting zone. Turn Weather follows "
         "time on to return to the authored schedule; use Edit weather schedule for saved "
         "changes."},
        {"control/Project directory", "Project directory",
         "Choose the folder for the editor project. For a new project this should be an empty "
         "folder separate from the original game dump."},
        {"control/Proportional editing", "Proportional editing",
         "Extend a geometry transform to nearby vertices using the configured radius/falloff. "
         "Check the affected area before applying."},
        {"control/Queue addition", "Queue addition",
         "Queue the reviewed overworld placement change. Use Apply and reload map to apply queued "
         "edits, then save/stage them through the project workflow."},
        {"control/Queue deletion", "Queue deletion",
         "Queue the reviewed overworld placement change. Use Apply and reload map to apply queued "
         "edits, then save/stage them through the project workflow."},
        {"control/Queue update", "Queue update",
         "Queue the reviewed overworld placement change. Use Apply and reload map to apply queued "
         "edits, then save/stage them through the project workflow."},
        {"control/Raise", "Raise",
         "Change the selected surface's height by the entered value or increment. Check adjacent "
         "boundaries and player collision after editing."},
        {"control/Raw surface attribute", "Raw surface attribute",
         "Inspect or edit the selected collision surface's numeric attribute. Use a known "
         "compatible value for the intended movement behavior."},
        {"control/Read current selection", "Read current selection",
         "Copy the current terrain selection into the explicit cell-selection controls."},
        {"control/Recalculate normals", "Recalculate normals",
         "Recompute normals for edited geometry. This can change shading along surfaces and "
         "seams."},
        {"control/Redo", "Redo",
         "Reapply a change undone in this editor. Making a new edit can replace the redo history."},
        {"control/Redo cry", "Redo cry",
         "Reapply a change undone in this editor. Making a new edit can replace the redo history."},
        {"control/Redo edit", "Redo edit",
         "Reapply a change undone in this editor. Making a new edit can replace the redo history."},
        {"control/Reduce background refresh", "Reduce background refresh",
         "Reduce rendering work through a frame cap or slower background refresh. This affects "
         "editor responsiveness and resource use."},
        {"control/Refit box to model", "Refit box to model",
         "Create or refit an approximate collision box around the selected model. Inspect the "
         "approximation; detailed geometry may need custom collision."},
        {"control/Refresh", "Refresh",
         "Inspect Pokemon interaction regions and feeding behavior. These are specialized model "
         "resources and preview controls."},
        {"control/Refresh catalog", "Refresh catalog",
         "Rebuild the displayed catalog from the current source. To view compiled project edits, "
         "stage them and reload staged assets."},
        {"control/Refresh destinations", "Refresh destinations",
         "Read available entrance destinations from the current project source. Choose the "
         "intended zone and entrance, then validate the connection."},
        {"control/Refresh items", "Refresh items",
         "Rebuild the displayed catalog from the current source. To view compiled project edits, "
         "stage them and reload staged assets."},
        {"control/Refresh map list", "Refresh map list",
         "Rebuild the displayed catalog from the current source. To view compiled project edits, "
         "stage them and reload staged assets."},
        {"control/Regenerate", "Regenerate",
         "Generate collision from the authored terrain using the reviewed settings. This replaces "
         "generated collision geometry, so inspect boundaries afterwards."},
        {"control/Regenerate from terrain...", "Regenerate from terrain...",
         "Generate collision from the authored terrain using the reviewed settings. This replaces "
         "generated collision geometry, so inspect boundaries afterwards."},
        {"control/Region", "Region",
         "Inspect/edit the encounter region's geometry and table assignment."},
        {"control/Region shape", "Region shape",
         "Edit the encounter region's position, rotation and dimensions. Use overlays to verify "
         "the actual covered area."},
        {"control/Regions", "Regions",
         "Inspect and paint Pokemon reaction regions. These masks control where different "
         "interaction categories apply."},
        {"control/Reload audio library", "Reload audio library",
         "Read the project's audio catalog so music, effects and cries can be browsed. Reload "
         "staged assets first when you need newly compiled source changes."},
        {"control/Reload from source", "Reload from source",
         "Reload this model from the current source assets. Save and stage pending edits as "
         "appropriate before replacing the preview."},
        {"control/Reload last PNG", "Reload last PNG",
         "Read the previously selected replacement PNG again after external edits. Review the new "
         "image before saving."},
        {"control/Reload staged assets", "Reload staged assets",
         "Save open project documents and reopen the editor using already staged assets. This "
         "does not compile new edits; use Stage and reload to include them in the source preview."},
        {"control/Remove bone track", "Remove bone track",
         "Remove this bone's animation track from the selected motion. Other bone tracks remain."},
        {"control/Remove collision", "Remove collision",
         "Remove the selected authored object's collision assignment. Visible geometry does not "
         "automatically provide game collision."},
        {"control/Remove mesh track", "Remove mesh track",
         "Remove the selected mesh's visibility track from the motion."},
        {"control/Remove second layer", "Remove second layer",
         "Remove the selected cells' secondary texture layer. Their base surface remains."},
        {"control/Remove selected material", "Remove selected material",
         "Remove the selected material if its references allow it. Reassign faces first when the "
         "material is still in use."},
        {"control/Remove selected part", "Remove selected part",
         "Remove the selected part from the assembled outfit preview. The source clothing asset "
         "remains available."},
        {"control/Remove texture", "Remove texture",
         "Remove the selected texture resource when allowed. Check material references before "
         "removing it."},
        {"control/Remove track", "Remove track",
         "Remove the selected material animation track. Its material properties then use remaining "
         "animation or base values."},
        {"control/Rendering", "Rendering",
         "Edit material render state such as blending, culling and depth behavior. Incorrect "
         "settings can hide surfaces or cause draw-order artifacts."},
        {"control/Repeat", "Repeat",
         "Repeat preview playback. Use track loop follows the audio file's authored loop points "
         "when available."},
        {"control/Replace PNG...", "Replace PNG...",
         "Choose a PNG to replace the selected image or texture. Inspect the preview and "
         "dimensions, then save and stage the edit."},
        {"control/Replace ground surface", "Replace ground surface",
         "Choose which authored ground should replace the selected map surface. Review export "
         "scope before compiling."},
        {"control/Replace with PNG...", "Replace with PNG...",
         "Choose a PNG to replace the selected image or texture. Inspect the preview and "
         "dimensions, then save and stage the edit."},
        {"control/Replace with flat ground", "Replace with flat ground",
         "Create a flat authored ground surface using the configured dimensions. Replacing "
         "existing authored ground changes the composition; inspect the selection first."},
        {"control/Reset all", "Reset all",
         "Reset this editor's changes to the loaded source values. Review the result before "
         "saving."},
        {"control/Reset all settings", "Reset all settings",
         "Reset this editor's changes to the loaded source values. Review the result before "
         "saving."},
        {"control/Reset feeding parameters", "Reset feeding parameters",
         "Reset the preview rectangle to the game's fixed feeding-area bounds. Saved feeding "
         "parameters and game assets are unchanged."},
        {"control/Reset game feeding area", "Reset game feeding area",
         "Reset the preview rectangle to the game's fixed feeding-area bounds. Saved feeding "
         "parameters and game assets are unchanged."},
        {"control/Reset grid controls", "Reset grid controls",
         "Return the grid input controls to their default values without applying a new grid."},
        {"control/Reset image", "Reset image",
         "Restore this resource to the loaded source version. Save and stage the reset if you want "
         "it in the project export."},
        {"control/Reset material", "Reset material",
         "Restore the selected material's edited settings to the loaded source values. Other "
         "materials are unaffected."},
        {"control/Reset outfit", "Reset outfit",
         "Reset the currently assembled preview outfit. This is separate from restoring palette "
         "resource edits."},
        {"control/Reset palette", "Reset palette",
         "Restore the selected palette to its loaded source colors."},
        {"control/Reset placement", "Reset placement",
         "Restore the selected placement to its loaded source transform/settings. Other placements "
         "keep their edits."},
        {"control/Reset source camera", "Reset source camera",
         "Restore the selected camera parameters from the source values. Review before saving an "
         "edited camera document."},
        {"control/Reset source feeding camera", "Reset source feeding camera",
         "Restore the selected camera parameters from the source values. Review before saving an "
         "edited camera document."},
        {"control/Reset test", "Reset test",
         "Reset the temporary test pose or interaction preview. Saved source settings are not "
         "replaced by the test reset."},
        {"control/Reset texture", "Reset texture",
         "Restore this resource to the loaded source version. Save and stage the reset if you want "
         "it in the project export."},
        {"control/Reset this mask", "Reset this mask",
         "Restore the current interaction mask to its loaded source values. Save and stage if you "
         "want to export the reset."},
        {"control/Reset this sample", "Reset this sample",
         "Restore this resource to the loaded source version. Save and stage the reset if you want "
         "it in the project export."},
        {"control/Reset this saved edit", "Reset this saved edit",
         "Restore the selected revision or resource as described in this panel. Check the scope "
         "before proceeding, then stage/build the resulting project state."},
        {"control/Reset this workspace layout", "Reset this workspace layout",
         "Restore the panel arrangement for the current workspace. Model and project edits are "
         "unaffected."},
        {"control/Reset track", "Reset track",
         "Restore this resource to the loaded source version. Save and stage the reset if you want "
         "it in the project export."},
        {"control/Reset zone schedule", "Reset zone schedule",
         "Restore this zone's five weather entries to the loaded project source. Other zones "
         "retain their edits; Undo can reverse the reset."},
        {"control/Resize", "Resize",
         "Choose scaling or resizing for the selected elements. Check the pivot and dimensions "
         "before applying the edit."},
        {"control/Restart", "Restart",
         "Restart the preview from its beginning. This does not remove animation keys or saved "
         "edits."},
        {"control/Restart idle", "Restart idle",
         "Restart the preview from its beginning. This does not remove animation keys or saved "
         "edits."},
        {"control/Restart player", "Restart player",
         "Restart the camera preview's player movement so you can inspect camera transitions "
         "again."},
        {"control/Restart send-out", "Restart send-out",
         "Control the battle/send-out preview. These actions do not author gameplay battle "
         "events."},
        {"control/Restore", "Restore",
         "Restore the selected revision or resource as described in this panel. Check the scope "
         "before proceeding, then stage/build the resulting project state."},
        {"control/Restore authored combiners", "Restore authored combiners",
         "Restore the selected material's combiner configuration to its loaded source settings."},
        {"control/Restore original", "Restore original",
         "Restore the selected revision or resource as described in this panel. Check the scope "
         "before proceeding, then stage/build the resulting project state."},
        {"control/Resume feeding", "Resume feeding",
         "Pause or resume the specialized Pokemon interaction preview without editing its saved "
         "parameters."},
        {"control/Resume preview", "Resume preview",
         "Pause or resume the specialized Pokemon interaction preview without editing its saved "
         "parameters."},
        {"control/Retry battle preview", "Retry battle preview",
         "Control the battle/send-out preview. These actions do not author gameplay battle "
         "events."},
        {"control/Return to Authoring", "Return to Authoring",
         "Return to the indicated workspace to continue authoring or select a different map. "
         "Respond to any pending-edit prompt before switching."},
        {"control/Return to free camera", "Return to free camera",
         "Leave player movement preview and return to viewport navigation. Map placements and "
         "entrances are unchanged."},
        {"control/Return to palettes", "Return to palettes",
         "Return from outfit preview to palette browsing/editing."},
        {"control/Review new map", "Review new map",
         "Calculate and review the new map plan from the selected template and entrance. Review "
         "the required runtime IDs before creating it."},
        {"control/Rotate", "Rotate",
         "Choose rotation handles for the current edit selection. Check the axis and pivot before "
         "rotating."},
        {"control/Rotate (E)", "Rotate (E)",
         "Choose rotation handles for the current edit selection. Check the axis and pivot before "
         "rotating."},
        {"control/Sample current frame", "Sample current frame",
         "Read the current animation pose into the editor fields before creating or adjusting a "
         "key."},
        {"control/Save", "Save",
         "Save this editor's document. Save as lets you choose another document file. This is "
         "separate from staging a project or exporting game assets."},
        {"control/Save Project", "Save Project",
         "Save the current edit documents. Stage Project compiles saved edits; Build game export "
         "writes the staged assets for use with the game."},
        {"control/Save as new asset", "Save as new asset",
         "Save the authored/extracted object as a reusable project asset. Add placements of the "
         "asset in a composition to use it in a map."},
        {"control/Save as...", "Save as...",
         "Save this editor's document. Save as lets you choose another document file. This is "
         "separate from staging a project or exporting game assets."},
        {"control/Save audio project...", "Save audio project...",
         "Save the current editor's editable document. In project mode use Save Project, then "
         "stage changes before building game resources."},
        {"control/Save cry project", "Save cry project",
         "Save this resource's edit document. Stage it through the project workflow when you want "
         "updated game assets."},
        {"control/Save cry project...", "Save cry project...",
         "Save the current editor's editable document. In project mode use Save Project, then "
         "stage changes before building game resources."},
        {"control/Save edits", "Save edits",
         "Save the current editor's editable document. In project mode use Save Project, then "
         "stage changes before building game resources."},
        {"control/Save image project", "Save image project",
         "Save this resource's edit document. Stage it through the project workflow when you want "
         "updated game assets."},
        {"control/Save palette to project", "Save palette to project",
         "Save the edited clothing palette. Palette changes can affect multiple clothing parts "
         "using the same colors; stage before building game resources."},
        {"control/Save palette...", "Save palette...",
         "Save the edited clothing palette. Palette changes can affect multiple clothing parts "
         "using the same colors; stage before building game resources."},
        {"control/Save patch", "Save patch",
         "Save the current editor's editable document. In project mode use Save Project, then "
         "stage changes before building game resources."},
        {"control/Save project", "Save project",
         "Save editable documents in the current project. Save does not stage assets or build game "
         "files. Use Stage Project next when you want compiled changes."},
        {"control/Save settings", "Save settings",
         "Save the current editor's editable document. In project mode use Save Project, then "
         "stage changes before building game resources."},
        {"control/Save to asset library", "Save to asset library",
         "Save the authored/extracted object as a reusable project asset. Add placements of the "
         "asset in a composition to use it in a map."},
        {"control/Saved edits", "Saved edits",
         "Inspect the project's saved editable documents. A saved document can differ from both "
         "the staged assets and the built export."},
        {"control/Scale", "Scale",
         "Choose scaling or resizing for the selected elements. Check the pivot and dimensions "
         "before applying the edit."},
        {"control/Scan coverage", "Scan coverage",
         "Inspect which game image resources the current scan recognized and any resources it "
         "could not decode."},
        {"control/Scene contents", "Scene contents",
         "Control which objects and animations are visible while inspecting the map. Hidden "
         "preview objects are not deleted from the game."},
        {"control/See through geometry", "See through geometry",
         "Allow inspection or selection through foreground geometry. Check what is selected before "
         "applying an edit."},
        {"control/See through scenery", "See through scenery",
         "Allow inspection or selection through foreground geometry. Check what is selected before "
         "applying an edit."},
        {"control/Select", "Select",
         "Use the selection tool rather than transforming the current UV selection."},
        {"control/Select all", "Select all",
         "Select every editable element in this view. Selection alone does not change game data."},
        {"control/Select all cells", "Select all cells",
         "Select every editable element in this view. Selection alone does not change game data."},
        {"control/Select assigned faces", "Select assigned faces",
         "Select faces already using the chosen material. This is useful for checking or changing "
         "a material assignment."},
        {"control/Select connected", "Select connected",
         "Select connected geometry or UV islands. Separate shells and UV seams can divide a model "
         "into several groups."},
        {"control/Select enabled meshes", "Select enabled meshes",
         "Select geometry from the mesh parts enabled in this panel."},
        {"control/Select these cells", "Select these cells",
         "Select the cells identified by the entered range or indices. This changes selection "
         "only."},
        {"control/Selected mesh", "Selected mesh",
         "Inspect the selected mesh and its material/resource bindings."},
        {"control/Selected region only", "Selected region only",
         "Configure spatial overlay rendering and picking. Enable only the layers needed for the "
         "task to make selection clearer."},
        {"control/Selection and whole-mesh attachment", "Selection and whole-mesh attachment",
         "Choose the geometry affected by skin-weight edits, including attaching whole enabled "
         "meshes to a bone."},
        {"control/Send model to Studio", "Send model to Studio",
         "Open the selected map or library model for editing in Studio. Shared-resource edits "
         "affect other uses of the same resource; the player model is excluded from map transfer."},
        {"control/Send new entry to Studio", "Send new entry to Studio",
         "Open the newly created Pokemon entry in Studio to inspect and edit its model resources."},
        {"control/Send to Studio", "Send to Studio",
         "Open the loaded model in Studio for material, texture, geometry, skeleton and motion "
         "editing. Changes affect the source resource and its shared uses."},
        {"control/Set key", "Set key",
         "Write a key at the selected frame using the current channel values. Check the selected "
         "motion, track and channel before saving."},
        {"control/Set new start here", "Set new start here",
         "Set the ruler's first endpoint at the current 3D cursor. Move the cursor to measure a "
         "distance."},
        {"control/Set selected weight", "Set selected weight",
         "Set the chosen bone's influence on the selected vertices using the entered weight."},
        {"control/Set simulated value", "Set simulated value",
         "Set or clear simulated runtime values used by the camera preview. These help inspect "
         "conditions without editing a game save."},
        {"control/Settings", "Settings",
         "Inspect settings for the current editor or project. Read the individual fields to "
         "distinguish game edits from preview and autosave preferences."},
        {"control/Shadow lighting", "Shadow lighting",
         "Inspect how the Pokemon shadow appears under the current preview lighting settings."},
        {"control/Shadow model", "Shadow model",
         "Choose which resource to inspect. The main model and its separate shadow can have "
         "different geometry and materials."},
        {"control/Shape and simplify selected cells", "Shape and simplify selected cells",
         "Use terrain shaping and geometry reduction tools on the selected region. Preview the "
         "affected cells before applying operations."},
        {"control/Shared sample uses", "Shared sample uses",
         "Inspect which cries reference the selected sound sample. Replacing a shared sample can "
         "affect several entries."},
        {"control/Shiny material colors", "Shiny material colors",
         "Edit supported shiny material color values. Check shiny textures and animation overrides "
         "separately."},
        {"control/Shiny textures", "Shiny textures",
         "Preview the Pokemon's shiny texture variant. This does not automatically create a new "
         "form or species entry."},
        {"control/Show UVs", "Show UVs",
         "Show interaction mask or UV overlays for inspection. Painting and applying edits are "
         "separate actions."},
        {"control/Show all", "Show all",
         "Show all available meshes in this preview. Visibility used for inspection is separate "
         "from saved visibility animation."},
        {"control/Show all reaction categories", "Show all reaction categories",
         "Show interaction mask or UV overlays for inspection. Painting and applying edits are "
         "separate actions."},
        {"control/Show bones through model", "Show bones through model",
         "Show skeletal structure or bone influence overlays for inspection. These overlays are "
         "not game geometry."},
        {"control/Show encounter regions", "Show encounter regions",
         "Enable the relevant map overlays so encounter coverage and surface attributes can be "
         "inspected."},
        {"control/Show feeding area", "Show feeding area",
         "Display the feeding-area overlay so you can inspect its position and dimensions."},
        {"control/Show ground types", "Show ground types",
         "Enable the relevant map overlays so encounter coverage and surface attributes can be "
         "inspected."},
        {"control/Show heatmap", "Show heatmap",
         "Color vertices by the selected influence so weight distribution is easier to inspect."},
        {"control/Show influenced vertices", "Show influenced vertices",
         "Show skeletal structure or bone influence overlays for inspection. These overlays are "
         "not game geometry."},
        {"control/Show interaction regions", "Show interaction regions",
         "Show interaction mask or UV overlays for inspection. Painting and applying edits are "
         "separate actions."},
        {"control/Show interactions", "Show interactions",
         "Enable interaction overlays such as entrances, NPCs, pickups and encounters. Layers can "
         "be visible without being selectable when locked."},
        {"control/Show map scenery", "Show map scenery",
         "Show visible map models alongside collision so you can compare geometry and movement "
         "boundaries."},
        {"control/Show range", "Show range",
         "Set whether the selected mesh is visible over the entered frame range. This edits "
         "visibility animation, not just the inspection view."},
        {"control/Show technical calls", "Show technical calls",
         "Include lower-level script calls in the interaction view when the simplified action list "
         "is insufficient."},
        {"control/Show transformed UVs", "Show transformed UVs",
         "Show UVs with the material transform applied for inspection. Be aware of the difference "
         "between base UV coordinates and material UV animation."},
        {"control/Show unused constants", "Show unused constants",
         "Show constant-color slots that the current texture combiners do not use. Editing an "
         "unused slot may have no visible effect."},
        {"control/Size and placement", "Size and placement",
         "Edit Pokemon size and placement-related model settings. Inspect the result in the "
         "relevant battle or model preview."},
        {"control/Skeletal animation", "Skeletal animation",
         "Enable this kind of animation in the viewport. Pausing or hiding animation does not "
         "remove its saved tracks or keys."},
        {"control/Skeleton", "Skeleton",
         "Inspect and edit bones and bind transforms. Bone changes can affect skinned geometry and "
         "all motions using the skeleton."},
        {"control/Skybox", "Skybox",
         "Toggle this rendering feature in the preview. Compare the appearance without changing "
         "the saved game material or environment."},
        {"control/Smooth mesh", "Smooth mesh",
         "Smooth the selected terrain heights. Check shorelines and intentional sharp edges before "
         "saving."},
        {"control/Snap", "Snap",
         "Constrain editing movement to the configured increments. This changes how manipulation "
         "works, not the source until you move something."},
        {"control/Snap arrow movement", "Snap arrow movement",
         "Constrain editing movement to the configured increments. This changes how manipulation "
         "works, not the source until you move something."},
        {"control/Soft diffuse", "Soft diffuse",
         "Toggle this rendering feature in the preview. Compare the appearance without changing "
         "the saved game material or environment."},
        {"control/Soft studio preset", "Soft studio preset",
         "Choose a preview lighting/background preset to inspect the model. This does not edit the "
         "game's environment lighting."},
        {"control/Source", "Source",
         "Inspect the source map and resources used by this authored composition."},
        {"control/Source & dependencies", "Source & dependencies",
         "Inspect the files and shared resources used by this model. Shared resources can affect "
         "multiple previews and placements."},
        {"control/Source archives", "Source archives",
         "Inspect the source archive selection for this viewer. In project mode sources are "
         "managed by the project."},
        {"control/Source record", "Source record",
         "Inspect the selected spatial region's source record and identifiers. Supported changes "
         "belong in the corresponding editor."},
        {"control/Stage Project", "Stage Project",
         "Save editor documents and compile them into the project's staged assets. Staging alone "
         "does not necessarily reload the open map or build the export folder."},
        {"control/Stage and reload", "Stage and reload",
         "Save open project documents, compile their changes, then reopen the editor using the "
         "updated staged assets. Build game export is a separate action."},
        {"control/Stage and return to map", "Stage and return to map",
         "Save and stage this model's project edits, then return to the map source preview. Shared "
         "model changes can affect other placements."},
        {"control/Staged resources", "Staged resources",
         "Inspect compiled resources that differ from the original dump. Build game export writes "
         "these staged resources to the export folder."},
        {"control/Start at map spawn", "Start at map spawn",
         "Place the viewing camera at the selected zone's default start whenever a map loads. This "
         "saved preference does not move any game placements."},
        {"control/Start at view target", "Start at view target",
         "Start player preview at the camera's current target. This is useful for checking scale "
         "and collision; it does not create an entrance or edit the map spawn."},
        {"control/Start from selected map", "Start from selected map",
         "Open the selected map's saved composition, or create one from the current project source. "
         "Saved compositions retain their source baseline. Review what the composition replaces."},
        {"control/Start player mode", "Start player mode",
         "Switch between walking preview and the free viewing camera. Player preview checks "
         "movement and collision but does not execute story scripts."},
        {"control/Start player preview", "Start player preview",
         "Enter player movement preview to check map scale and collision. Scripts, story "
         "progression and full gameplay are not simulated."},
        {"control/Stop", "Stop", "Stop audio preview playback. The saved sound is unchanged."},
        {"control/Stop clip", "Stop clip",
         "Stop audio preview playback. The saved sound is unchanged."},
        {"control/Stop music", "Stop music",
         "Stop audio preview playback. The saved sound is unchanged."},
        {"control/Stop send-out", "Stop send-out",
         "Control the battle/send-out preview. These actions do not author gameplay battle "
         "events."},
        {"control/Studio", "Studio",
         "Edit a model's materials, textures, geometry, skeleton and motions. Shared-resource "
         "edits can affect every placement using that resource."},
        {"control/Subdivide selected cells", "Subdivide selected cells",
         "Split selected cells to provide more vertices for shaping. More detail increases "
         "geometry size; check the export after substantial changes."},
        {"control/Summary memory details", "Summary memory details",
         "Inspect estimated model resource usage. Use export validation to check actual "
         "compatibility."},
        {"control/Surfaces", "Surfaces",
         "Browse reusable surface assets for terrain cells. Choose one, then preview or paint it "
         "onto the selected cells."},
        {"control/Swap endpoints", "Swap endpoints",
         "Clear the ruler measurement or exchange its endpoints. Map geometry is unchanged."},
        {"control/Test selected bone", "Test selected bone",
         "Temporarily pose the selected bone to inspect skinning. Reset the test pose before "
         "judging the bind geometry."},
        {"control/Texture combiners", "Texture combiners",
         "Edit how texture stages combine textures, lighting and constant colors into a surface. "
         "Each stage's inputs, operation and output affect later stages."},
        {"control/Textures", "Textures",
         "Inspect the material's texture inputs and image previews. Select the corresponding "
         "Studio texture unit to edit an assignment."},
        {"control/Textures and UVs", "Textures and UVs",
         "Choose a texture unit, inspect its selected texture, replace the image and edit UV "
         "scale, rotation or offset. UV animation can override the base transform."},
        {"control/Tile grid", "Tile grid",
         "Configure the authored surface grid used to arrange and shape ground cells."},
        {"control/Tilt X +", "Tilt X +",
         "Tilt selected cells along the indicated horizontal axis. Inspect edges and collision "
         "after changing the slope."},
        {"control/Tilt X -", "Tilt X -",
         "Tilt selected cells along the indicated horizontal axis. Inspect edges and collision "
         "after changing the slope."},
        {"control/Tilt Z +", "Tilt Z +",
         "Tilt selected cells along the indicated horizontal axis. Inspect edges and collision "
         "after changing the slope."},
        {"control/Tilt Z -", "Tilt Z -",
         "Tilt selected cells along the indicated horizontal axis. Inspect edges and collision "
         "after changing the slope."},
        {"control/Triangle edges", "Triangle edges",
         "Draw mesh edges over the preview so you can inspect topology. This is a display option."},
        {"control/Triangle outlines", "Triangle outlines",
         "Draw mesh edges over the preview so you can inspect topology. This is a display option."},
        {"control/Triangles", "Triangles",
         "Select triangles/faces rather than individual vertices. Face selection is useful for "
         "material assignment, extraction and collision editing."},
        {"control/Trigger dimensions and endpoints", "Trigger dimensions and endpoints",
         "Edit entrance trigger bounds and relevant endpoint/arrival fields. Check both the "
         "visible trigger and destination in the map."},
        {"control/Turn ring", "Turn ring",
         "Choose rotation handles for the current edit selection. Check the axis and pivot before "
         "rotating."},
        {"control/Undo", "Undo",
         "Undo the most recent change in this editor. Other editors keep their own histories. "
         "Check the result before saving again."},
        {"control/Undo camera / edits", "Undo camera / edits",
         "Undo the most recent change in this editor. Other editors keep their own histories. "
         "Check the result before saving again."},
        {"control/Undo cry", "Undo cry",
         "Undo the most recent change in this editor. Other editors keep their own histories. "
         "Check the result before saving again."},
        {"control/Undo edit", "Undo edit",
         "Undo the most recent change in this editor. Other editors keep their own histories. "
         "Check the result before saving again."},
        {"control/Undo feeding / edits", "Undo feeding / edits",
         "Undo the most recent change in this editor. Other editors keep their own histories. "
         "Check the result before saving again."},
        {"control/Update an existing GARC", "Update an existing GARC",
         "Write edited resources into a chosen compatible archive. This updates that selected "
         "archive rather than producing a separate override folder."},
        {"control/Use 3D cursor position", "Use 3D cursor position",
         "Use the 3D cursor as the selected placement's position. Check the height and collision "
         "surface before saving the placement."},
        {"control/Use as material donor", "Use as material donor",
         "Use this loaded model as a source for borrowing compatible material settings in Studio."},
        {"control/Use chosen texture as second layer", "Use chosen texture as second layer",
         "Assign the chosen texture as a blend layer on the selected ground. Adjust blend values "
         "to control the transition."},
        {"control/Use current frame", "Use current frame",
         "Copy the playback position into the key-frame field. This does not create a key until "
         "you use Set key."},
        {"control/Use distance / scale overrides", "Use distance / scale overrides",
         "Use the configured overrides in the feeding preview. Compare them with source values "
         "before saving supported parameter edits."},
        {"control/Use dump archive", "Use dump archive",
         "Return to the corresponding archive in the current dump/project source."},
        {"control/Use dump palette", "Use dump palette",
         "Return to the corresponding archive in the current dump/project source."},
        {"control/Use track loop", "Use track loop",
         "Repeat preview playback. Use track loop follows the audio file's authored loop points "
         "when available."},
        {"control/Use view target", "Use view target",
         "Use the current viewing target for the edited position field. Confirm the selected "
         "entrance/region before applying it."},
        {"control/Use zone start", "Use zone start",
         "Use the zone's default start for the edited position. Check the resulting arrival "
         "placement and facing."},
        {"control/Validate destinations", "Validate destinations",
         "Check that edited entrance destinations resolve to valid zones and entrance records. "
         "Validation does not run the game's scripts."},
        {"control/Vertex (1)", "Vertex (1)",
         "Select individual vertices. Use the selection tools before moving or transforming them."},
        {"control/Vertex colors", "Vertex colors",
         "Toggle this rendering feature in the preview. Compare the appearance without changing "
         "the saved game material or environment."},
        {"control/Vertices", "Vertices",
         "Select individual vertices. Use the selection tools before moving or transforming them."},
        {"control/Viewport display", "Viewport display",
         "Adjust preview rendering, including the sky, weather, vertex colors and wireframe. These "
         "controls do not rewrite material settings."},
        {"control/Visibility", "Visibility",
         "Inspect per-mesh visibility or visibility animation. Preview hiding and saved visibility "
         "tracks are different operations."},
        {"control/Weather effects", "Weather effects",
         "Toggle this rendering feature in the preview. Compare the appearance without changing "
         "the saved game material or environment."},
        {"control/Weather follows time", "Weather follows time",
         "Choose preview weather automatically from the selected zone's five-slot schedule using "
         "Time of day. Turn this off to compare a manual weather override."},
        {"control/Weights", "Weights",
         "Edit how selected vertices follow skeleton bones. Keep weights normalized and verify "
         "deformation in animation preview."},
        {"control/Whole motion", "Whole motion",
         "Set the visibility edit's frame range to cover the entire selected motion."},
        {"control/Wild encounters", "Wild encounters",
         "Open the encounter editor for the loaded map. Encounter region geometry and Pokemon "
         "tables are separate parts of the workflow."},
        {"control/Wireframe", "Wireframe",
         "Draw mesh edges over the preview so you can inspect topology. This is a display option."},
        {"control/Write depth", "Write depth",
         "Control whether this material writes to the depth buffer. This is a saved render-state "
         "change and can affect overlapping transparent surfaces."},
        {"control/Write game files...", "Write game files...",
         "Export this document's edited game resources using the chosen destination mode. Review "
         "whether the mode creates an override or updates an existing archive."},
        {"control/Write to working dump", "Write to working dump",
         "Write edited game resources into the selected working dump. Check that this is a working "
         "copy before exporting."},
        {"control/X-ray selection", "X-ray selection",
         "Allow inspection or selection through foreground geometry. Check what is selected before "
         "applying an edit."},
        {"main/##choose-folder", "Choose dump folder",
         "Choose the extracted dump folder for standalone inspection. Project workflows manage "
         "their source through Project setup."},
        {"main/Map", "Map",
         "Select and load a named map. The selection can differ from the map currently in the "
         "viewport until Load map completes."},
        {"main/Preview", "Preview",
         "Preview player movement, map weather, lighting and music. Weather schedule edits are "
         "saved data; most other controls here change only the preview."},
        {"main/View", "View",
         "Configure scene visibility, framing, navigation help and performance. These settings "
         "help inspection and do not delete game objects."},
        {"map_authoring_ground/##terrain-tool", "Terrain tool",
         "Choose a terrain editing tool. The tool's controls determine whether you select, shape "
         "or paint cells; inspect the selection before applying edits."},
        {"material_editor/Lighting", "Lighting",
         "Inspect material lighting inputs and settings. Material edits can affect the saved game "
         "appearance."},
        {"model_workspace/##visible", "Mesh visibility",
         "Show or hide this mesh in the preview. Use visibility motion editing when you want an "
         "animated visibility change in game."},
        {"project_import/Archive", "Archive",
         "Import a compatible source archive into the project. Stage and reload pending edits "
         "first. This changes the project source resources rather than creating an ordinary "
         "editable document."},
        {"refresh_inspector/Cameras", "Cameras",
         "Open the specialized Pokemon interaction camera preview. These controls are separate "
         "from map camera regions."},
        {"weather_editor/Undo", "Undo",
         "Undo the latest weather schedule change, including changes in another zone edited during "
         "this session."},
        {"workspace/-1", "Project setup walkthrough",
         "Create a separate project folder and choose your extracted game dump containing romfs, "
         "or open an existing editor project. Project files hold your edits; staged assets are "
         "compiled from them. The walkthrough resumes after project opening restarts the editor."},
        {"workspace/0", "Maps walkthrough",
         "Load a map, navigate from its start and Ctrl+click a surface to inspect it. Area "
         "contains Map, Preview and View tabs; Scene lists objects; Spatial exposes interaction "
         "layers. Follow the highlighted controls or open another panel to learn it in context."},
        {"workspace/1", "Models walkthrough",
         "Choose a Pokemon, character or clothing resource from your project. Load it to inspect "
         "its parts and animation, then send it to Studio for edits. A model resource can be "
         "shared by several game placements."},
        {"workspace/2", "Studio walkthrough",
         "Start by sending a model here from Maps or Models. Choose a material on the left; use "
         "the detail tabs for geometry, skeleton, motions and Blender exchange. Edits affect the "
         "loaded resource, including other placements that share it. Save, stage and reload to "
         "inspect compiled results."},
        {"workspace/3", "Authoring walkthrough",
         "Create or open a composition, select a source map and gather reusable assets. Extract "
         "objects, shape or paint ground, then add placements. Check export before staging. A "
         "visible building still needs separate collision and entrance data to behave as a "
         "building in game."},
        {"workspace/4", "Collision walkthrough",
         "Load a map first, then compare movement surfaces with visible scenery. Select triangles "
         "or vertices, adjust geometry or attributes and inspect boundaries. Camera movement alone "
         "does not prove that game collision is correct."},
        {"workspace/5", "Cameras walkthrough",
         "Load the target map and select a camera region. Inspect the active/selected camera, "
         "preview movement and edit supported settings. Capture current view writes the viewing "
         "pose into camera data; ordinary free-camera navigation does not."},
        {"workspace/6", "Images walkthrough",
         "Select an image resource, compare the original and edited versions, and export or "
         "replace PNGs. Check transparency, dimensions and stored texture format before saving. "
         "Save and stage changes before building game files."},
        {"workspace/7", "Audio walkthrough",
         "Choose music, a sound or Pokemon cry and listen to the original first. Import a "
         "replacement, inspect its conversion and preview it before applying. Save editable "
         "documents, stage them, then build the game export."},
    };
    return topics;
}
}
