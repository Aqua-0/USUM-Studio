bl_info = {"name": "USUMStudio Mesh Exchange", "author": "USUMStudio contributors", "version": (1, 1, 0), "blender": (4, 2, 0), "location": "File > Import/Export; 3D View > Sidebar > Collision / USUMStudio", "description": "Edit collision and source-linked object geometry for USUMStudio", "category": "Import-Export"}

import json
import math
from pathlib import Path
import re
import bpy
import bmesh
from bpy.props import StringProperty, EnumProperty
from bpy_extras.io_utils import ImportHelper, ExportHelper

ROLE_ITEMS = [(name, label, "") for name, label in [("ground", "Ground"), ("wall", "Wall"), ("surf", "Surf boundary"), ("ride", "Ride barrier"), ("mudsdale", "Mudsdale barrier")]]
MATERIAL = re.compile(r"collision_(ground|wall|surf|ride|mudsdale)_attr_([0-9]+)$")


def active_collection(context):
    name = context.scene.get("usum_collision_collection", "")
    collection = bpy.data.collections.get(name)
    if collection is None or "usum_baseline" not in collection:
        raise ValueError("Import a USUMStudio collision OBJ first")
    return collection


def palette_color(attribute):
    import colorsys
    hue = (.31 + attribute * .6180339887498949) % 1
    saturation = (.65, .85, .48)[attribute % 3]
    value = .95 if attribute % 2 else .78
    return (*colorsys.hsv_to_rgb(hue, saturation, value), 1)


def collision_material(name):
    match = MATERIAL.fullmatch(name)
    if not match or int(match[2]) > 0xffffffff:
        raise ValueError("Use a collision_TYPE_attr_NUMBER material (attribute 0 to 4294967295)")
    material = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    material.diffuse_color = palette_color(int(match[2]))
    material.use_nodes = True
    shader = material.node_tree.nodes.get("Principled BSDF")
    if shader:
        shader.inputs["Base Color"].default_value = material.diffuse_color
    return material


def import_reference(path, context):
    vertices, groups = [], []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == 'v':
            x, y, z = map(float, fields[1:])
            vertices.append((x, -z, y))
        elif fields[0] == 'o':
            groups.append((fields[1], []))
        elif fields[0] == 'f':
            groups[-1][1].append(tuple(int(value) - 1 for value in fields[1:]))
    collection = bpy.data.collections.new("Map reference")
    context.scene.collection.children.link(collection)
    collection["usum_reference"] = True
    for name, faces in groups:
        used = sorted({index for face in faces for index in face})
        indices = {value: index for index, value in enumerate(used)}
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata([vertices[index] for index in used], [], [tuple(indices[index] for index in face) for face in faces])
        mesh.update()
        obj = bpy.data.objects.new(name, mesh)
        collection.objects.link(obj)
        obj.hide_select = True
    return collection


def import_collision(path, context=None):
    context = context or bpy.context
    vertices, records, members, palette = [], [], set(), set()
    baseline, token, material, member = None, None, None, None
    header = complete = False
    surface_names = {}
    reference = None
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[:2] == ["#", "usum_collision"]:
            if fields[2:] != ["1"]:
                raise ValueError("Unsupported collision OBJ version")
            header = True
        elif fields[:2] == ["#", "baseline"]:
            baseline = fields[2]
        elif fields[:2] == ["#", "reference"]:
            name = line.split("reference", 1)[1].strip()
            if Path(name).name != name or "/" in name or "\\" in name:
                raise ValueError("Map reference must be beside the collision OBJ")
            reference = Path(path).parent / name
            if not reference.is_file():
                raise ValueError("Keep the map reference OBJ beside the collision OBJ")
        elif fields[:2] == ["#", "surface_name"]:
            surface_names[fields[2]] = " ".join(fields[3:])
        elif fields[:2] == ["#", "complete"]:
            complete = True
        elif fields[0] == "o":
            member = int(fields[1].removeprefix("terrain_"))
            members.add(member)
        elif fields[0] == "g":
            token = fields[1].removeprefix("collision_")
        elif fields[0] == "usemtl":
            material = fields[1]
            if not MATERIAL.fullmatch(material):
                raise ValueError("Invalid collision material")
            palette.add(material)
        elif fields[0] == "v":
            point = tuple(float(x) for x in fields[1:])
            if len(point) != 3 or not all(math.isfinite(x) for x in point):
                raise ValueError("Invalid collision vertex")
            vertices.append(point)
        elif fields[0] == "f":
            if member is None or token is None or material is None or len(fields) != 4:
                raise ValueError("Missing collision identity or non-triangle face")
            records.append((member, token, material, [vertices[int(x.split('/')[0])-1] for x in fields[1:]]))
    if not header or not baseline or len(baseline) != 64 or not complete:
        raise ValueError("Export this OBJ from USUMStudio's Collision workspace")
    tokens = [record[1] for record in records]
    if len(tokens) != len(set(tokens)) or "new" in tokens:
        raise ValueError("Import the original USUMStudio export; import edited results back into USUMStudio first")
    if context.object and context.object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='DESELECT')
    collection = bpy.data.collections.new("USUM Collision")
    context.scene.collection.children.link(collection)
    collection["usum_baseline"] = baseline
    collection["usum_tokens"] = json.dumps(tokens)
    collection["usum_original_faces"] = json.dumps({r[1]: r[3] for r in records})
    collection["usum_members"] = json.dumps(sorted(members))
    collection["usum_source"] = str(path)
    collection["usum_surface_names"] = json.dumps(surface_names)
    context.scene.usum_collision_member = str(min(members)) if members else "0"
    context.scene["usum_collision_collection"] = collection.name
    session = int(baseline[:7], 16) + 1
    for attribute in range(39):
        palette.add(f"collision_ground_attr_{attribute}")
    for kind, _, _ in ROLE_ITEMS:
        palette.add(f"collision_{kind}_attr_0")
    materials = [collision_material(name) for name in sorted(palette)]
    material_indices = {m.name: i for i, m in enumerate(materials)}
    for terrain in sorted(members):
        local_vertices, faces, local_records, welded = [], [], [], {}
        for record_id, record in enumerate(records):
            if record[0] != terrain:
                continue
            indices = []
            kind = MATERIAL.fullmatch(record[2])[1]
            for x, y, z in record[3]:
                key = (kind, x, y, z)
                if key not in welded:
                    welded[key] = len(local_vertices)
                    local_vertices.append((x, -z, y))
                indices.append(welded[key])
            faces.append(indices)
            local_records.append((record_id + 1, material_indices[record[2]]))
        mesh = bpy.data.meshes.new(f"Terrain collision {terrain}")
        mesh.from_pydata(local_vertices, [], faces)
        mesh.update()
        obj = bpy.data.objects.new(f"Terrain collision {terrain}", mesh)
        collection.objects.link(obj)
        obj["usum_member"] = terrain
        obj["usum_baseline"] = baseline
        for mat in materials:
            mesh.materials.append(mat)
        for name in ("collision_face_id", "collision_member", "collision_session"):
            mesh.attributes.new(name, 'INT', 'FACE')
        for polygon, (identity, mat_index) in zip(mesh.polygons, local_records):
            mesh.attributes["collision_face_id"].data[polygon.index].value = identity
            mesh.attributes["collision_member"].data[polygon.index].value = terrain + 1
            mesh.attributes["collision_session"].data[polygon.index].value = session
            polygon.material_index = mat_index
        obj.select_set(True)
        context.view_layer.objects.active = obj
    if reference:
        import_reference(reference, context)
    if not bpy.app.background:
        for window in context.window_manager.windows:
            for area in window.screen.areas:
                if area.type == 'VIEW_3D':
                    area.spaces.active.clip_end = max(area.spaces.active.clip_end, 1000000)
                    area.spaces.active.shading.color_type = 'MATERIAL'
                    region = next((region for region in area.regions if region.type == 'WINDOW'), None)
                    if region:
                        with context.temp_override(window=window, area=area, region=region):
                            bpy.ops.view3d.view_selected(use_all_regions=False)
    return collection


def distance_to_original(points, original):
    return min(sum((points[i][k] - original[(i+shift) % 3][k]) ** 2 for i in range(3) for k in range(3)) for shift in range(3))


def export_collision(path, context=None):
    context = context or bpy.context
    if context.object and context.object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    collection = active_collection(context)
    baseline = collection["usum_baseline"]
    tokens = json.loads(collection["usum_tokens"])
    original = json.loads(collection["usum_original_faces"])
    members = set(json.loads(collection["usum_members"]))
    session = int(baseline[:7], 16) + 1
    records = []
    for obj in collection.all_objects:
        if obj.type != 'MESH':
            continue
        if obj.get("usum_baseline") != baseline:
            raise ValueError(f"Attach {obj.name} using the Collision sidebar before exporting")
        if any(mod.show_viewport for mod in obj.modifiers):
            raise ValueError(f"Apply modifiers on {obj.name} before exporting collision")
        mesh = obj.data
        mesh.calc_loop_triangles()
        identities = mesh.attributes.get("collision_face_id")
        terrain_ids = mesh.attributes.get("collision_member")
        sessions = mesh.attributes.get("collision_session")
        mirrored = obj.matrix_world.determinant() < 0
        for triangle in mesh.loop_triangles:
            polygon = mesh.polygons[triangle.polygon_index]
            identity = identities.data[polygon.index].value if identities else 0
            face_session = sessions.data[polygon.index].value if sessions else 0
            if face_session not in (0, session) or identity < 0 or identity > len(tokens):
                raise ValueError("Collision face belongs to another export or has an invalid ID")
            terrain_id = terrain_ids.data[polygon.index].value if terrain_ids else 0
            terrain = terrain_id - 1 if terrain_id else obj.get("usum_member")
            if terrain not in members:
                raise ValueError("Collision geometry belongs to an unexported terrain resource")
            if polygon.material_index >= len(obj.material_slots) or obj.material_slots[polygon.material_index].material is None:
                raise ValueError(f"Assign collision materials to every face of {obj.name}")
            material = obj.material_slots[polygon.material_index].material.name
            match = MATERIAL.fullmatch(material)
            if not match or int(match[2]) > 0xffffffff:
                raise ValueError(f"Invalid collision material: {material}")
            indices = list(triangle.vertices)
            if mirrored:
                indices.reverse()
            points = []
            for index in indices:
                world = obj.matrix_world @ mesh.vertices[index].co
                point = (world.x, world.z, -world.y)
                if not all(math.isfinite(x) and abs(x) < 1e9 for x in point):
                    raise ValueError("Collision coordinates are outside the supported range")
                points.append(point)
            records.append([terrain, tokens[identity-1] if identity else "new", material, points])
    candidates = {}
    for index, record in enumerate(records):
        if record[1] != "new":
            candidates.setdefault(record[1], []).append(index)
    for token, indices in candidates.items():
        keep = min(indices, key=lambda index: distance_to_original(records[index][3], original[token]))
        for index in indices:
            if index != keep:
                records[index][1] = "new"
    output = ["# usum_collision 1", f"# baseline {baseline}"]
    vertex_index = 1
    for terrain, token, material, points in records:
        output += [f"o terrain_{terrain}", f"g collision_{token}", f"usemtl {material}"]
        for point in points:
            output.append("v " + " ".join(format(x, '.9g') for x in point))
        output.append(f"f {vertex_index} {vertex_index+1} {vertex_index+2}")
        vertex_index += 3
    output.append("# complete")
    destination = Path(path)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    temporary.write_text("\n".join(output) + "\n", encoding="utf-8")
    temporary.replace(destination)
    return len(records)


class USUM_OT_import_collision(bpy.types.Operator, ImportHelper):
    bl_idname = "usum.import_collision"
    bl_label = "USUMStudio Collision OBJ"
    bl_options = {'UNDO'}
    filename_ext = ".obj"
    filter_glob: StringProperty(default="*.obj", options={'HIDDEN'})

    def execute(self, context):
        try:
            import_collision(self.filepath, context)
            return {'FINISHED'}
        except Exception as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class USUM_OT_export_collision(bpy.types.Operator, ExportHelper):
    bl_idname = "usum.export_collision"
    bl_label = "USUMStudio Collision OBJ"
    filename_ext = ".obj"
    filter_glob: StringProperty(default="*.obj", options={'HIDDEN'})

    def execute(self, context):
        try:
            count = export_collision(self.filepath, context)
            self.report({'INFO'}, f"Exported {count} collision triangles")
            return {'FINISHED'}
        except Exception as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class USUM_OT_assign_surface(bpy.types.Operator):
    bl_idname = "usum.assign_collision_surface"
    bl_label = "Assign to selected faces"
    bl_options = {'UNDO'}

    def execute(self, context):
        try:
            attribute = int(context.scene.usum_collision_attribute)
            if not 0 <= attribute <= 0xffffffff:
                raise ValueError("Attribute must be from 0 to 4294967295")
            material = collision_material(f"collision_{context.scene.usum_collision_kind}_attr_{attribute}")
            collection = active_collection(context)
            objects = context.objects_in_mode_unique_data if context.mode == 'EDIT_MESH' else context.selected_objects
            for obj in objects:
                if obj.type != 'MESH' or obj.name not in collection.all_objects:
                    continue
                index = obj.data.materials.find(material.name)
                if index < 0:
                    obj.data.materials.append(material)
                    index = len(obj.data.materials) - 1
                if obj.mode == 'EDIT':
                    mesh = bmesh.from_edit_mesh(obj.data)
                    for face in mesh.faces:
                        if face.select:
                            face.material_index = index
                    bmesh.update_edit_mesh(obj.data)
                else:
                    for face in obj.data.polygons:
                        if face.select:
                            face.material_index = index
            return {'FINISHED'}
        except Exception as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class USUM_OT_attach_collision(bpy.types.Operator):
    bl_idname = "usum.attach_collision"
    bl_label = "Attach selected meshes"
    bl_options = {'UNDO'}

    def execute(self, context):
        try:
            collection = active_collection(context)
            member = int(context.scene.usum_collision_member)
            if member not in json.loads(collection["usum_members"]):
                raise ValueError("Choose a terrain member from the imported collision collection")
            for obj in context.selected_objects:
                if obj.type != 'MESH':
                    continue
                if obj.get("usum_baseline", collection["usum_baseline"]) != collection["usum_baseline"]:
                    raise ValueError("Do not mix geometry from different collision exports")
                if obj.name not in collection.objects:
                    collection.objects.link(obj)
                obj["usum_member"] = member
                obj["usum_baseline"] = collection["usum_baseline"]
                if not obj.data.materials:
                    obj.data.materials.append(collision_material("collision_ground_attr_0"))
            return {'FINISHED'}
        except Exception as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


SURFACE_ITEMS = [("0", "Tall grass (0)", "")]
SURFACE_KEY = None


def surface_items(self, context):
    global SURFACE_ITEMS, SURFACE_KEY
    try:
        names = active_collection(context)["usum_surface_names"]
        if names != SURFACE_KEY:
            SURFACE_ITEMS = [(key, f"{name} ({key})", "") for key, name in json.loads(names).items()] or [("0", "Attribute 0", "")]
            SURFACE_KEY = names
    except (ValueError, AttributeError, KeyError):
        pass
    return SURFACE_ITEMS


def choose_surface(self, context):
    self.usum_collision_attribute = self.usum_collision_surface


class USUM_PT_collision(bpy.types.Panel):
    bl_label = "USUMStudio Collision"
    bl_idname = "USUM_PT_collision"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "Collision"

    def draw(self, context):
        layout = self.layout
        layout.operator("usum.import_collision", text="Import collision OBJ")
        layout.operator("usum.export_collision", text="Export edited collision OBJ")
        layout.separator()
        layout.prop(context.scene, "usum_collision_kind", text="Type")
        row = layout.row()
        row.enabled = context.scene.usum_collision_kind == "ground"
        row.prop(context.scene, "usum_collision_surface", text="Surface")
        layout.prop(context.scene, "usum_collision_attribute", text="Attribute ID")
        layout.operator("usum.assign_collision_surface")
        layout.separator()
        try:
            collection = active_collection(context)
            layout.label(text="Terrain members: " + ", ".join(map(str, json.loads(collection["usum_members"]))))
        except ValueError:
            pass
        layout.prop(context.scene, "usum_collision_member", text="Terrain member")
        layout.operator("usum.attach_collision")
        layout.label(text="Material Preview shows surface colors.")


def import_object_file(path, context):
    import shlex
    lines = iter(Path(path).read_text(encoding="utf-8").splitlines())
    if next(lines, "") != "USUMSTUDIO_OBJECT 1":
        raise ValueError("Unsupported USUMStudio object exchange")
    signature_line = shlex.split(next(lines))
    if signature_line[0] != "signature":
        raise ValueError("Missing object source signature")
    signature = signature_line[1]
    header = next(lines).split()
    if header[0] != "sections":
        raise ValueError("Missing object sections")
    sections = []
    for index in range(int(header[1])):
        section = shlex.split(next(lines))
        if section[0] != "section" or int(section[1]) != index:
            raise ValueError("Invalid object section")
        vertices = []
        for _ in range(int(section[4])):
            row = next(lines).split()
            if len(row) != 16:
                raise ValueError("Invalid object vertex")
            values = tuple(map(float, row[:15]))
            if not all(math.isfinite(v) for v in values):
                raise ValueError("Object contains nonfinite vertex data")
            vertices.append((values, int(row[15])))
        faces = [tuple(map(int, next(lines).split())) for _ in range(int(section[5]))]
        if any(len(f) != 3 or any(i < 0 or i >= len(vertices) for i in f) for f in faces):
            raise ValueError("Invalid object triangle")
        sections.append((section[2], section[3], vertices, faces))
    if next(lines, "") != "end" or any(line.strip() for line in lines):
        raise ValueError("Unexpected object exchange data")
    collection = bpy.data.collections.new("USUMStudio object")
    context.scene.collection.children.link(collection)
    collection["usum_object_signature"] = signature
    collection["usum_object_sections"] = len(sections)
    collection["usum_object_names"] = json.dumps([s[0] for s in sections])
    context.scene["usum_object_collection"] = collection.name
    for obj in context.selected_objects:
        obj.select_set(False)
    for index, (name, texture, vertices, faces) in enumerate(sections):
        material = bpy.data.materials.new(name or f"Section {index}")
        material["usum_object_section"] = index
        material["usum_object_signature"] = signature
        material.use_nodes = True
        if texture:
            texture_path = Path(path).parent / texture
            if texture_path.is_file():
                node = material.node_tree.nodes.new("ShaderNodeTexImage")
                node.image = bpy.data.images.load(str(texture_path), check_existing=True)
                shader = material.node_tree.nodes.get("Principled BSDF")
                material.node_tree.links.new(node.outputs["Color"], shader.inputs["Base Color"])
                material.node_tree.links.new(node.outputs["Alpha"], shader.inputs["Alpha"])
        if not faces:
            continue
        mesh = bpy.data.meshes.new(name or f"Section {index}")
        welded, mapping, coordinates = {}, [], []
        for v, _ in vertices:
            point = (v[0], -v[2], v[1])
            if point not in welded:
                welded[point] = len(coordinates)
                coordinates.append(point)
            mapping.append(welded[point])
        mesh.from_pydata(coordinates, [], [tuple(mapping[i] for i in face) for face in faces])
        mesh.materials.append(material)
        for channel in range(3):
            mesh.uv_layers.new(name=f"UV{channel}")
        colors = mesh.color_attributes.new(name="Game color", type='BYTE_COLOR', domain='CORNER')
        normals = []
        for polygon, face in zip(mesh.polygons, faces):
            polygon.use_smooth = True
            for loop, index_in_source in zip(polygon.loop_indices, face):
                v, color = vertices[index_in_source]
                for channel in range(3):
                    mesh.uv_layers[f"UV{channel}"].data[loop].uv = v[9 + 2 * channel:11 + 2 * channel]
                colors.data[loop].color_srgb = tuple(((color >> (8 * channel)) & 255) / 255 for channel in range(4))
                normals.append((v[3], -v[5], v[4]))
        mesh.normals_split_custom_set(normals)
        mesh.uv_layers.active_index = 0
        mesh.update()
        obj = bpy.data.objects.new(name or "Object section", mesh)
        collection.objects.link(obj)
        obj.select_set(True)
        context.view_layer.objects.active = obj
    return collection


def object_collection(context):
    collection = bpy.data.collections.get(context.scene.get("usum_object_collection", ""))
    if collection is None or "usum_object_signature" not in collection:
        raise ValueError("Import a USUMStudio object first")
    return collection


def export_object_file(path, context):
    collection = object_collection(context)
    if context.object and context.object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    context.view_layer.update()
    signature = collection["usum_object_signature"]
    sections = [([], [], {}) for _ in range(collection["usum_object_sections"])]
    for obj in collection.all_objects:
        if obj.type != 'MESH':
            continue
        if any(mod.show_viewport for mod in obj.modifiers):
            raise ValueError(f"Apply visible modifiers on {obj.name} before exporting")
        mesh = obj.data
        for channel in range(3):
            if f"UV{channel}" not in mesh.uv_layers:
                raise ValueError(f"Restore the UV{channel} layer on {obj.name}")
        matrix = obj.matrix_world
        if abs(matrix.determinant()) < 1e-12:
            raise ValueError(f"Object {obj.name} has a zero-scale transform")
        normal_matrix = matrix.to_3x3().inverted().transposed()
        mesh.calc_loop_triangles()
        try:
            mesh.calc_tangents(uvmap="UV0")
            tangents = True
        except RuntimeError:
            tangents = False
        colors = mesh.color_attributes.get("Game color")
        for triangle in mesh.loop_triangles:
            if triangle.material_index >= len(obj.material_slots):
                raise ValueError(f"Assign an exported game material to every face of {obj.name}")
            material = obj.material_slots[triangle.material_index].material
            if material is None or material.get("usum_object_signature") != signature:
                raise ValueError("Use the game materials from this object export")
            section = material.get("usum_object_section", -1)
            if section < 0 or section >= len(sections):
                raise ValueError("Invalid game material section")
            vertices, faces, lookup = sections[section]
            loops = list(triangle.loops)
            if matrix.determinant() < 0:
                loops.reverse()
            face = []
            for loop_index in loops:
                loop = mesh.loops[loop_index]
                point = matrix @ mesh.vertices[loop.vertex_index].co
                normal = (normal_matrix @ mesh.corner_normals[loop_index].vector).normalized()
                tangent = (matrix.to_3x3() @ loop.tangent).normalized() if tangents else (0, 0, 0)
                values = [point.x, point.z, -point.y, normal.x, normal.z, -normal.y,
                          tangent[0], tangent[2], -tangent[1]]
                for channel in range(3):
                    values.extend(mesh.uv_layers[f"UV{channel}"].data[loop_index].uv)
                color = 0xffffffff
                if colors:
                    color_index = loop_index if colors.domain == 'CORNER' else loop.vertex_index
                    rgba = colors.data[color_index].color_srgb
                    color = sum(max(0, min(255, round(value * 255))) << (8 * channel) for channel, value in enumerate(rgba))
                if not all(math.isfinite(value) and abs(value) < 1e7 for value in values):
                    raise ValueError("Object has an invalid position, normal or UV")
                key = (*values, color)
                if key not in lookup:
                    lookup[key] = len(vertices)
                    vertices.append(key)
                face.append(lookup[key])
            faces.append(face)
    if not any(faces for _, faces, _ in sections):
        raise ValueError("Keep at least one object face")
    names = json.loads(collection["usum_object_names"])
    output = ["USUMSTUDIO_OBJECT 1", "signature " + json.dumps(signature), f"sections {len(sections)}"]
    for index, (vertices, faces, _) in enumerate(sections):
        if len(vertices) > 65536:
            raise ValueError(f"Section {names[index]} exceeds 65,536 vertices; simplify it before exporting")
        output.append(f'section {index} {json.dumps(names[index], ensure_ascii=False)} "" {len(vertices)} {len(faces)}')
        output.extend(" ".join(format(value, '.9g') for value in vertex[:15]) + f" {vertex[15]}" for vertex in vertices)
        output.extend(" ".join(map(str, face)) for face in faces)
    output.append("end")
    Path(path).write_text("\n".join(output) + "\n", encoding="utf-8")


class USUM_OT_import_object(bpy.types.Operator, ImportHelper):
    bl_idname = "usum.import_object"
    bl_label = "Import USUMStudio object"
    bl_options = {'REGISTER', 'UNDO'}
    filename_ext = ".usum-object"
    filter_glob: StringProperty(default="*.usum-object", options={'HIDDEN'})

    def execute(self, context):
        try:
            import_object_file(self.filepath, context)
            return {'FINISHED'}
        except (ValueError, OSError, IndexError, StopIteration) as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class USUM_OT_export_object(bpy.types.Operator, ExportHelper):
    bl_idname = "usum.export_object"
    bl_label = "Export edited USUMStudio object"
    filename_ext = ".usum-object"
    filter_glob: StringProperty(default="*.usum-object", options={'HIDDEN'})

    def execute(self, context):
        try:
            export_object_file(self.filepath, context)
            return {'FINISHED'}
        except (ValueError, OSError, IndexError) as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class USUM_PT_object(bpy.types.Panel):
    bl_label = "USUMStudio Objects"
    bl_idname = "USUM_PT_object"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "USUMStudio"

    def draw(self, context):
        self.layout.operator("usum.import_object")
        self.layout.operator("usum.export_object")
        self.layout.label(text="Edit geometry and UV0 / UV1 / UV2.")
        self.layout.label(text="Keep the exported game materials.")
        self.layout.label(text="Export includes the imported collection.")
        self.layout.label(text="Material Preview approximates the base texture.")

def menu_import(self, context):
    self.layout.operator(USUM_OT_import_object.bl_idname, text="USUMStudio Object (.usum-object)")
    self.layout.operator(USUM_OT_import_collision.bl_idname, text="USUMStudio Collision (.obj)")


def menu_export(self, context):
    self.layout.operator(USUM_OT_export_object.bl_idname, text="USUMStudio Object (.usum-object)")
    self.layout.operator(USUM_OT_export_collision.bl_idname, text="USUMStudio Collision (.obj)")


CLASSES = (USUM_OT_import_collision, USUM_OT_export_collision, USUM_OT_assign_surface, USUM_OT_attach_collision, USUM_PT_collision, USUM_OT_import_object, USUM_OT_export_object, USUM_PT_object)


def register():
    for cls in CLASSES:
        bpy.utils.register_class(cls)
    bpy.types.Scene.usum_collision_surface = EnumProperty(items=surface_items, update=choose_surface)
    bpy.types.Scene.usum_collision_kind = EnumProperty(items=ROLE_ITEMS, default="ground")
    bpy.types.Scene.usum_collision_attribute = StringProperty(default="0")
    bpy.types.Scene.usum_collision_member = StringProperty(default="0")
    bpy.types.TOPBAR_MT_file_import.append(menu_import)
    bpy.types.TOPBAR_MT_file_export.append(menu_export)


def unregister():
    bpy.types.TOPBAR_MT_file_import.remove(menu_import)
    bpy.types.TOPBAR_MT_file_export.remove(menu_export)
    for name in ("usum_collision_kind", "usum_collision_attribute", "usum_collision_member", "usum_collision_surface"):
        delattr(bpy.types.Scene, name)
    for cls in reversed(CLASSES):
        bpy.utils.unregister_class(cls)


if __name__ == "__main__":
    register()
