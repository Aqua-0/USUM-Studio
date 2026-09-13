# SPDX-License-Identifier: GPL-3.0-or-later
# See LICENSE in this directory.

bl_info = {"name": "USUM Studio model exchange", "author": "USUM Studio", "version": (1, 2, 0), "blender": (4, 2, 0), "location": "File > Import/Export", "category": "Import-Export"}

import json
import math
import shlex
from pathlib import Path
import bpy
from bpy.props import StringProperty
from bpy_extras.io_utils import ImportHelper, ExportHelper
from mathutils import Matrix, Vector, Euler

AXES = Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
INVERSE_AXES = AXES.inverted()


def read_model(path):
    tokens = iter(shlex.split(Path(path).read_text(encoding="utf-8")))
    def expect(value):
        if next(tokens) != value:
            raise ValueError("Invalid model exchange: expected " + value)
    expect("USUMSTUDIO_MODEL")
    if int(next(tokens)) != 1:
        raise ValueError("Unsupported model exchange version")
    expect("source")
    model = {"source": next(tokens), "bones": [], "meshes": []}
    expect("bones")
    for _ in range(int(next(tokens))):
        expect("bone")
        bone = {"name": next(tokens), "parent": int(next(tokens)), "flags": int(next(tokens))}
        bone["transform"] = [float(next(tokens)) for _ in range(9)]
        model["bones"].append(bone)
    expect("meshes")
    for _ in range(int(next(tokens))):
        expect("mesh")
        mesh = {"name": next(tokens), "influences": int(next(tokens))}
        vertices, indices = int(next(tokens)), int(next(tokens))
        expect("channels")
        mesh["formats"] = [[int(next(tokens)), int(next(tokens))] for _ in range(7)]
        expect("texture")
        mesh["texture"] = next(tokens)
        mesh["vertices"] = []
        for _ in range(vertices):
            expect("vertex")
            channels = [[float(next(tokens)) for _ in range(4)] for _ in range(7)]
            joints = [int(next(tokens)) for _ in range(4)]
            weights = [float(next(tokens)) for _ in range(4)]
            mesh["vertices"].append({"channels": channels, "joints": joints, "weights": weights})
        expect("indices")
        mesh["indices"] = [int(next(tokens)) for _ in range(indices)]
        model["meshes"].append(mesh)
    expect("end")
    if next(tokens, None) is not None:
        raise ValueError("Unexpected model exchange data")
    return model


def quote(value):
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'


def write_model(path, model):
    with Path(path).open("w", encoding="utf-8", newline="\n") as out:
        out.write("USUMSTUDIO_MODEL 1\nsource " + model["source"] + "\nbones " + str(len(model["bones"])) + "\n")
        for bone in model["bones"]:
            out.write("bone " + quote(bone["name"]) + " " + str(bone["parent"]) + " " + str(bone["flags"]) + " " + " ".join(format(v, ".9g") for v in bone["transform"]) + "\n")
        out.write("meshes " + str(len(model["meshes"])) + "\n")
        for mesh in model["meshes"]:
            out.write("mesh " + quote(mesh["name"]) + " " + str(mesh["influences"]) + " " + str(len(mesh["vertices"])) + " " + str(len(mesh["indices"])) + "\nchannels " + " ".join(str(v) for pair in mesh["formats"] for v in pair) + "\ntexture " + quote(mesh["texture"]) + "\n")
            for vertex in mesh["vertices"]:
                values = [v for channel in vertex["channels"] for v in channel]
                out.write("vertex " + " ".join(format(v, ".9g") for v in values) + " " + " ".join(str(v) for v in vertex["joints"]) + " " + " ".join(format(v, ".9g") for v in vertex["weights"]) + "\n")
            out.write("indices " + " ".join(str(v) for v in mesh["indices"]) + "\n")
        out.write("end\n")


def divisor(fmt):
    return (127.0, 255.0, 32767.0, 1.0)[fmt]


def read_preview(path, model):
    sidecar = Path(str(path) + ".preview")
    if not sidecar.is_file():
        return None
    lines = sidecar.read_text(encoding="utf-8").splitlines()
    if lines[0] != "USUMSTUDIO_PREVIEW 1":
        raise ValueError("Unsupported Studio preview metadata; export the model again")
    source, count = shlex.split(lines[1])
    if source != model["source"] or int(count) != len(model["meshes"]) or len(lines) != int(count) + 2:
        raise ValueError("Preview metadata belongs to a different model; export again")
    values = [list(map(float, line.split())) for line in lines[2:]]
    if any(len(v) != 15 or not all(math.isfinite(n) for n in v) for v in values):
        raise ValueError("Invalid Studio preview metadata")
    return values


def preview_material(path, data, preview):
    material = bpy.data.materials.new(data["name"] + " preview")
    material.use_nodes = True
    nodes, links = material.node_tree.nodes, material.node_tree.links
    shader = nodes.get("Principled BSDF")
    shader.inputs["Roughness"].default_value = 1
    texture = Path(path).parent / data["texture"]
    if not data["texture"] or not texture.is_file():
        return material
    image = nodes.new("ShaderNodeTexImage")
    image.image = bpy.data.images.load(str(texture), check_existing=True)
    links.new(image.outputs["Color"], shader.inputs["Base Color"])
    if not preview:
        return material
    source, wrap_u, wrap_v, filtering, function, reference, blend = map(int, preview[:7])
    if source > 2:
        material["studio_preview_note"] = "Projected texture coordinates require Studio preview"
        return material
    def math_node(operation, a, b=0):
        node = nodes.new("ShaderNodeMath")
        node.operation = operation
        for socket, value in zip(node.inputs, (a, b)):
            if isinstance(value, (int, float)):
                socket.default_value = value
            else:
                links.new(value, socket)
        return node.outputs[0]
    uv = nodes.new("ShaderNodeUVMap")
    uv.uv_map = "UV" + str(source)
    split = nodes.new("ShaderNodeSeparateXYZ")
    links.new(uv.outputs["UV"], split.inputs[0])
    u, v = split.outputs["X"], math_node('SUBTRACT', 1, split.outputs["Y"])
    combine = nodes.new("ShaderNodeCombineXYZ")
    border = 1
    for axis, row, wrap in ((0, preview[7:11], wrap_u), (1, preview[11:15], wrap_v)):
        value = math_node('ADD', math_node('ADD', math_node('MULTIPLY', u, row[0]), math_node('MULTIPLY', v, row[1])), row[2])
        if wrap == 2:
            value = math_node('FRACT', value)
        elif wrap == 3:
            value = math_node('PINGPONG', value, 1)
        elif wrap == 1:
            inside = math_node('MULTIPLY', math_node('SUBTRACT', 1, math_node('LESS_THAN', value, 0)), math_node('SUBTRACT', 1, math_node('GREATER_THAN', value, 1)))
            border = math_node('MULTIPLY', border, inside)
        if axis == 1:
            value = math_node('SUBTRACT', 1, value)
        links.new(value, combine.inputs[axis])
    links.new(combine.outputs[0], image.inputs["Vector"])
    image.extension = 'EXTEND'
    image.interpolation = 'Closest' if filtering == 0 else 'Linear'
    alpha = math_node('MULTIPLY', image.outputs["Alpha"], border)
    sampled_alpha = alpha
    threshold = reference / 255
    if function == 0:
        alpha = 0
    elif function != 1:
        if function in (2, 3):
            equal = nodes.new("ShaderNodeMath")
            equal.operation = 'COMPARE'
            links.new(alpha, equal.inputs[0])
            equal.inputs[1].default_value = threshold
            equal.inputs[2].default_value = 0.5 / 255
            alpha = equal.outputs[0] if function == 2 else math_node('SUBTRACT', 1, equal.outputs[0])
        else:
            alpha = math_node('LESS_THAN' if function in (4, 7) else 'GREATER_THAN', alpha, threshold)
            if function in (5, 7):
                alpha = math_node('SUBTRACT', 1, alpha)
    elif blend == 0x01010000:
        alpha = border
    if function != 1 and blend != 0x01010000:
        alpha = math_node('MULTIPLY', sampled_alpha, alpha)
    if isinstance(alpha, (int, float)):
        shader.inputs["Alpha"].default_value = alpha
    else:
        links.new(alpha, shader.inputs["Alpha"])
    if hasattr(material, "surface_render_method"):
        material.surface_render_method = 'DITHERED'
    else:
        material.blend_method = 'HASHED'
    material["studio_preview_note"] = "Base texture, native UV transform and alpha approximation; use Studio for game shaders"
    return material


def import_model(path):
    model = read_model(path)
    previews = read_preview(path, model)
    if bpy.context.object and bpy.context.object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    collection = bpy.data.collections.new(Path(path).stem)
    bpy.context.scene.collection.children.link(collection)
    collection["usum_source"] = model["source"]
    collection["usum_bones"] = json.dumps(model["bones"])
    collection["usum_meshes"] = json.dumps([{k: v for k, v in mesh.items() if k != "vertices"} for mesh in model["meshes"]])
    rig = None
    if model["bones"]:
        armature = bpy.data.armatures.new(collection.name + " skeleton")
        rig = bpy.data.objects.new(armature.name, armature)
        collection.objects.link(rig)
        rig["usum_skeleton"] = True
        rig.show_in_front = True
        bpy.ops.object.select_all(action='DESELECT')
        rig.select_set(True)
        bpy.context.view_layer.objects.active = rig
        bpy.ops.object.mode_set(mode='EDIT')
        matrices = []
        for data in model["bones"]:
            transform = data["transform"]
            matrix = Euler(transform[3:6], 'XYZ').to_matrix().to_4x4()
            matrix.translation = Vector(transform[6:9])
            if data["parent"] >= 0:
                matrix = matrices[data["parent"]] @ matrix
            matrices.append(matrix)
        for index, data in enumerate(model["bones"]):
            bone = armature.edit_bones.new(data["name"])
            bone.head = AXES @ matrices[index].translation
            direction = (AXES @ matrices[index]).to_3x3() @ Vector((0, 1, 0))
            lengths = [(matrices[i].translation - matrices[index].translation).length for i, b in enumerate(model["bones"]) if b["parent"] == index]
            length = max(.5, min((v for v in lengths if v > .01), default=3.0))
            bone.tail = bone.head + direction.normalized() * length
            if data["parent"] >= 0:
                bone.parent = armature.edit_bones[model["bones"][data["parent"]]["name"]]
            bone.matrix = AXES @ matrices[index]
            bone.length = length
        bpy.ops.object.mode_set(mode='OBJECT')
        rig["usum_bind_matrices"] = json.dumps({bone.name: [v for row in (INVERSE_AXES @ rig.matrix_world @ bone.matrix_local) for v in row] for bone in armature.bones})
    for index, data in enumerate(model["meshes"]):
        mesh = bpy.data.meshes.new(data["name"])
        positions = [AXES @ Vector(v["channels"][0][:3]) for v in data["vertices"]]
        triangles = [data["indices"][i:i + 3] for i in range(0, len(data["indices"]), 3)]
        mesh.from_pydata(positions, [], triangles)
        mesh.update()
        obj = bpy.data.objects.new(data["name"], mesh)
        collection.objects.link(obj)
        obj["usum_mesh"] = index
        for channel in range(7):
            vector = mesh.attributes.new("studio_c" + str(channel), 'FLOAT_VECTOR', 'POINT')
            fourth = mesh.attributes.new("studio_w" + str(channel), 'FLOAT', 'POINT')
            vector = mesh.attributes["studio_c" + str(channel)]
            fourth = mesh.attributes["studio_w" + str(channel)]
            for i, vertex in enumerate(data["vertices"]):
                vector.data[i].vector = vertex["channels"][channel][:3]
                fourth.data[i].value = vertex["channels"][channel][3]
        for slot in range(4):
            mesh.attributes.new("studio_joint" + str(slot), 'INT', 'POINT')
            mesh.attributes.new("studio_weight" + str(slot), 'FLOAT', 'POINT')
            joints = mesh.attributes["studio_joint" + str(slot)]
            weights = mesh.attributes["studio_weight" + str(slot)]
            for i, vertex in enumerate(data["vertices"]):
                joints.data[i].value = vertex["joints"][slot]
                weights.data[i].value = vertex["weights"][slot]
        for channel in range(4, 7):
            fmt, elements = data["formats"][channel]
            if elements < 2:
                continue
            layer = mesh.uv_layers.new(name="UV" + str(channel - 4))
            scale = divisor(fmt)
            for loop in mesh.loops:
                uv = data["vertices"][loop.vertex_index]["channels"][channel]
                layer.data[loop.index].uv = (uv[0] / scale, 1 - uv[1] / scale)
        if data["formats"][3][1]:
            color = mesh.color_attributes.new(name="Color", type='FLOAT_COLOR', domain='CORNER')
            scale = divisor(data["formats"][3][0])
            for loop in mesh.loops:
                values = data["vertices"][loop.vertex_index]["channels"][3]
                color.data[loop.index].color = tuple(values[i] / scale if i < data["formats"][3][1] else 1 for i in range(4))
        if data["formats"][1][1] >= 3:
            normals = [(AXES.to_3x3() @ Vector(v["channels"][1][:3])).normalized() for v in data["vertices"]]
            for polygon in mesh.polygons:
                polygon.use_smooth = True
            mesh.normals_split_custom_set_from_vertices(normals)
            corner_normals = [n.vector.copy() for n in mesh.corner_normals]
            point_normals = [v.normal.copy() for v in mesh.vertices]
            baseline = mesh.attributes.new("studio_normal", 'FLOAT_VECTOR', 'CORNER')
            for i, normal in enumerate(corner_normals):
                baseline.data[i].vector = normal
            baseline = mesh.attributes.new("studio_point_normal", 'FLOAT_VECTOR', 'POINT')
            for i, normal in enumerate(point_normals):
                baseline.data[i].vector = normal
        if rig and data["influences"]:
            obj.parent = rig
            groups = [obj.vertex_groups.new(name=bone["name"]) for bone in model["bones"]]
            for v, vertex in enumerate(data["vertices"]):
                for bone, weight in zip(vertex["joints"], vertex["weights"]):
                    if weight > 0:
                        groups[bone].add([v], weight, 'REPLACE')
            modifier = obj.modifiers.new("Skin", 'ARMATURE')
            modifier.object = rig
        material = preview_material(path, data, previews[index] if previews else None)
        mesh.materials.append(material)
    bpy.ops.object.select_all(action='DESELECT')
    for obj in collection.objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = next((o for o in collection.objects if o.type == 'MESH'), rig)
    for area in bpy.context.screen.areas if bpy.context.screen else []:
        if area.type == 'VIEW_3D':
            area.spaces.active.clip_end = 100000
    return collection


def active_model_collection():
    obj = bpy.context.object
    candidates = [c for c in bpy.data.collections if "usum_source" in c and (obj is None or obj.name in c.all_objects)]
    if len(candidates) != 1:
        raise ValueError("Select an object from the USUM model you want to export")
    return candidates[0]


def export_model(path, collection=None):
    if bpy.context.object and bpy.context.object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    collection = collection or active_model_collection()
    original_bones = json.loads(collection["usum_bones"])
    metadata = json.loads(collection["usum_meshes"])
    rigs = [o for o in collection.all_objects if o.type == 'ARMATURE' and o.get("usum_skeleton")]
    rig = rigs[0] if len(rigs) == 1 else None
    if original_bones and rig is None:
        raise ValueError("Keep the exported armature")
    bones = []
    bone_names = [b["name"] for b in original_bones]
    if rig:
        for name in bone_names:
            if name not in rig.data.bones:
                raise ValueError("Restore existing game bone: " + name + ". Existing bones cannot be renamed or removed.")
        pending = [b.name for b in rig.data.bones if b.name not in bone_names]
        while pending:
            added = [name for name in pending if rig.data.bones[name].parent and rig.data.bones[name].parent.name in bone_names]
            if not added:
                raise ValueError("New bones must descend from the existing root")
            bone_names.extend(added)
            pending = [name for name in pending if name not in added]
        globals_ = {name: INVERSE_AXES @ rig.matrix_world @ rig.data.bones[name].matrix_local for name in bone_names}
        baseline = json.loads(rig.get("usum_bind_matrices", "{}"))
        unchanged = {name: baseline.get(name) == [v for row in matrix for v in row] for name, matrix in globals_.items()}
        for index, name in enumerate(bone_names):
            bone = rig.data.bones[name]
            parent = bone_names.index(bone.parent.name) if bone.parent else -1
            if parent >= index:
                raise ValueError("Parent must precede child in the game skeleton: " + name)
            if index < len(original_bones) and parent == original_bones[index]["parent"] and unchanged[name] and (not bone.parent or unchanged[bone.parent.name]):
                bones.append(dict(original_bones[index]))
                continue
            local = globals_[bone.parent.name].inverted() @ globals_[name] if bone.parent else globals_[name]
            if local.to_3x3().determinant() <= 0:
                raise ValueError("Mirrored bone transforms are unsupported: " + name)
            rotation = list(local.to_euler('XYZ'))
            if index < len(original_bones):
                original_rotation = original_bones[index]["transform"][3:6]
                matrix = Euler(original_rotation, 'XYZ').to_matrix()
                actual = local.to_3x3().normalized()
                if max(abs(matrix[r][c] - actual[r][c]) for r in range(3) for c in range(3)) < 2e-5:
                    rotation = original_rotation
            translation = list(local.translation)
            values = [1.0, 1.0, 1.0] + rotation + translation
            if index < len(original_bones):
                old = original_bones[index]
                values = [a if abs(a - b) <= 1e-5 * max(1, abs(a)) else b for a, b in zip(old["transform"], values)]
                flags = old["flags"]
            else:
                flags = 0
            bones.append({"name": name, "parent": parent, "flags": flags, "transform": values})
    objects = {}
    for obj in collection.all_objects:
        if obj.type != 'MESH':
            continue
        if "usum_mesh" not in obj:
            raise ValueError("Join new geometry into an exported mesh object before exporting: " + obj.name)
        index = int(obj["usum_mesh"])
        if index in objects or index < 0 or index >= len(metadata):
            raise ValueError("Keep one object per native mesh slot; join duplicated geometry into its original object")
        objects[index] = obj
    if len(objects) != len(metadata):
        raise ValueError("Keep every exported mesh object; use a visibility track to hide a whole mesh")
    result = {"source": collection["usum_source"], "bones": bones, "meshes": []}
    skin_modifiers = [(m, m.show_viewport) for obj in objects.values() for m in obj.modifiers if m.type == 'ARMATURE']
    for modifier, _ in skin_modifiers:
        modifier.show_viewport = False
    pose = rig.data.pose_position if rig else None
    if rig:
        rig.data.pose_position = 'REST'
    try:
        bpy.context.view_layer.update()
        depsgraph = bpy.context.evaluated_depsgraph_get()
        for index, meta in enumerate(metadata):
            obj = objects[index]
            evaluated = obj.evaluated_get(depsgraph)
            mesh = evaluated.to_mesh(preserve_all_data_layers=True, depsgraph=depsgraph)
            try:
                mesh.calc_loop_triangles()
                if not mesh.loop_triangles:
                    raise ValueError("Native mesh cannot be empty: " + obj.name)
                normal_matrix = INVERSE_AXES.to_3x3() @ evaluated.matrix_world.to_3x3().inverted().transposed()
                tangent_matrix = INVERSE_AXES.to_3x3() @ evaluated.matrix_world.to_3x3()
                position_matrix = INVERSE_AXES @ evaluated.matrix_world
                geometry_changed = [v for triangle in mesh.loop_triangles for v in triangle.vertices] != meta["indices"]
                original_positions = mesh.attributes.get("studio_c0")
                if original_positions is None:
                    geometry_changed = True
                else:
                    for v in mesh.vertices:
                        original_position = Vector(original_positions.data[v.index].vector)
                        if ((position_matrix @ v.co) - original_position).length > 1e-5 * max(1, original_position.length):
                            geometry_changed = True
                            break
                uv = mesh.uv_layers.get("UV0")
                old_uv = mesh.attributes.get("studio_c4")
                if uv and old_uv:
                    scale = divisor(meta["formats"][4][0])
                    for loop in mesh.loops:
                        a = uv.data[loop.index].uv
                        b = old_uv.data[loop.vertex_index].vector
                        if abs(a.x * scale - b.x) > 1e-6 or abs((1 - a.y) * scale - b.y) > 1e-6:
                            geometry_changed = True
                            break
                tangents = False
                if geometry_changed and meta["formats"][2][1] >= 3 and mesh.uv_layers.get("UV0"):
                    mesh.calc_tangents(uvmap="UV0")
                    tangents = True
                group_names = {g.index: g.name for g in obj.vertex_groups}
                bone_indices = {name: i for i, name in enumerate(bone_names)}
                vertices = [None] * len(mesh.vertices)
                variants = {}
                indices = []
                def vertex_data(vertex_index, loop_index=None):
                    vertex = mesh.vertices[vertex_index]
                    channels = []
                    for channel in range(7):
                        vec = mesh.attributes.get("studio_c" + str(channel))
                        fourth = mesh.attributes.get("studio_w" + str(channel))
                        channels.append(list(vec.data[vertex_index].vector) + [fourth.data[vertex_index].value] if vec and fourth else [0.0] * 4)
                    original = [v[:] for v in channels]
                    channels[0][:3] = position_matrix @ vertex.co
                    def direction(channel, vector):
                        vector = vector.normalized()
                        candidate = Vector(original[channel][:3])
                        if candidate.length and (candidate.normalized() - vector).length < 2e-4:
                            channels[channel][:3] = candidate
                        else:
                            channels[channel][:3] = vector * divisor(meta["formats"][channel][0])
                    if meta["formats"][1][1] >= 3:
                        normal = mesh.corner_normals[loop_index].vector if loop_index is not None else vertex.normal
                        baseline = mesh.attributes.get("studio_normal" if loop_index is not None else "studio_point_normal")
                        normal_index = loop_index if loop_index is not None else vertex_index
                        if geometry_changed or not baseline or (normal - baseline.data[normal_index].vector).length > 1e-6:
                            direction(1, normal_matrix @ normal)
                    if tangents and loop_index is not None:
                        direction(2, tangent_matrix @ mesh.loops[loop_index].tangent)
                    for channel in range(4, 7):
                        layer = mesh.uv_layers.get("UV" + str(channel - 4))
                        if layer and loop_index is not None and meta["formats"][channel][1] >= 2:
                            uv = layer.data[loop_index].uv
                            scale = divisor(meta["formats"][channel][0])
                            channels[channel][:2] = [uv.x * scale, (1 - uv.y) * scale]
                    color = mesh.color_attributes.get("Color")
                    if color and loop_index is not None and meta["formats"][3][1]:
                        values = color.data[loop_index if color.domain == 'CORNER' else vertex_index].color
                        channels[3] = [v * divisor(meta["formats"][3][0]) for v in values]
                    for channel in range(7):
                        fmt, elements = meta["formats"][channel]
                        for k in range(elements):
                            value = channels[channel][k]
                            if fmt != 3:
                                value = round(value)
                            old = original[channel][k]
                            channels[channel][k] = old if abs(value - old) <= 1e-6 * max(1, abs(old)) else value
                    weights = sorted(((bone_indices[group_names[g.group]], g.weight) for g in vertex.groups if g.weight > 1e-8 and group_names.get(g.group) in bone_indices), key=lambda v: -v[1])
                    if len(weights) > meta["influences"]:
                        raise ValueError(obj.name + ": vertex " + str(vertex_index) + " exceeds its native influence limit (" + str(meta["influences"]) + ")")
                    if meta["influences"] and not weights:
                        raise ValueError(obj.name + ": vertex " + str(vertex_index) + " needs a bone weight")
                    total = sum(w for _, w in weights)
                    previous_joints = [mesh.attributes.get("studio_joint" + str(i)) for i in range(4)]
                    previous_weights = [mesh.attributes.get("studio_weight" + str(i)) for i in range(4)]
                    if all(previous_joints) and all(previous_weights):
                        old_joints = [a.data[vertex_index].value for a in previous_joints]
                        old_weights = [a.data[vertex_index].value for a in previous_weights]
                        current = dict(weights)
                        previous = {b: w for b, w in zip(old_joints, old_weights) if w > 0}
                        if current.keys() == previous.keys() and all(abs(current[b] - previous[b]) < 1e-7 for b in current):
                            return {"channels": channels, "joints": old_joints, "weights": old_weights}
                    return {"channels": channels, "joints": [b for b, _ in weights] + [0] * (4 - len(weights)), "weights": [w / total for _, w in weights] + [0.0] * (4 - len(weights))}
                reverse = evaluated.matrix_world.to_3x3().determinant() < 0
                for triangle in mesh.loop_triangles:
                    loops = list(triangle.loops)
                    if reverse:
                        loops.reverse()
                    for loop_index in loops:
                        v = mesh.loops[loop_index].vertex_index
                        data = vertex_data(v, loop_index)
                        key = (v, tuple(x for c in data["channels"] for x in c), tuple(data["joints"]), tuple(data["weights"]))
                        if key not in variants:
                            if vertices[v] is None:
                                vertices[v] = data
                                variants[key] = v
                            else:
                                variants[key] = len(vertices)
                                vertices.append(data)
                        indices.append(variants[key])
                for v in range(len(mesh.vertices)):
                    if vertices[v] is None:
                        vertices[v] = vertex_data(v)
                if len(vertices) > 65536:
                    raise ValueError(obj.name + ": UV/normal seams produce more than 65536 native vertices")
                result["meshes"].append(dict(meta, vertices=vertices, indices=indices))
            finally:
                evaluated.to_mesh_clear()
    finally:
        for modifier, visible in skin_modifiers:
            modifier.show_viewport = visible
        if rig:
            rig.data.pose_position = pose
        bpy.context.view_layer.update()
    write_model(path, result)
    return result



def read_motion(path):
    tokens = iter(shlex.split(Path(path).read_text(encoding='utf-8')))
    def expect(word):
        if next(tokens) != word:
            raise ValueError('Expected ' + word + ' in motion exchange')
    def curves(count):
        result = []
        for _ in range(count):
            expect('curve')
            result.append([[float(next(tokens)) for _ in range(3)] for _ in range(int(next(tokens)))])
        return result
    expect('USUMSTUDIO_MOTION')
    if int(next(tokens)) != 1:
        raise ValueError('Unsupported motion exchange version')
    motion = {}
    for key in ('source', 'model', 'name'):
        expect(key)
        motion[key] = next(tokens)
    expect('clock')
    motion['frames'], motion['looping'] = int(next(tokens)), int(next(tokens))
    expect('bones')
    motion['bones'] = []
    for _ in range(int(next(tokens))):
        expect('bone')
        motion['bones'].append({'name': next(tokens), 'axis': int(next(tokens)), 'curves': curves(9)})
    expect('materials')
    motion['materials'] = []
    for _ in range(int(next(tokens))):
        expect('material')
        track = {'kind': int(next(tokens)), 'material': next(tokens), 'slot': int(next(tokens)), 'curves': curves(5)}
        expect('textures')
        track['textures'] = [[int(next(tokens)), next(tokens)] for _ in range(int(next(tokens)))]
        motion['materials'].append(track)
    expect('visibility')
    motion['visibility'] = []
    for _ in range(int(next(tokens))):
        expect('mesh')
        motion['visibility'].append({'mesh': next(tokens), 'frames': [int(next(tokens)) for _ in range(int(next(tokens)))]})
    expect('end')
    if next(tokens, None) is not None:
        raise ValueError('Unexpected motion data')
    return motion


def write_motion(path, motion):
    def number(value):
        return format(value, '.9g')
    with Path(path).open('w', encoding='utf-8', newline='\n') as out:
        out.write('USUMSTUDIO_MOTION 1\n')
        for key in ('source', 'model', 'name'):
            out.write(key + ' ' + quote(motion[key]) + '\n')
        out.write('clock %d %d\nbones %d\n' % (motion['frames'], motion['looping'], len(motion['bones'])))
        def curves(values):
            for curve in values:
                out.write('curve ' + str(len(curve)) + ''.join(' ' + ' '.join(number(v) for v in key) for key in curve) + '\n')
        for track in motion['bones']:
            out.write('bone ' + quote(track['name']) + ' ' + str(track['axis']) + '\n')
            curves(track['curves'])
        out.write('materials %d\n' % len(motion['materials']))
        for track in motion['materials']:
            out.write('material %d %s %d\n' % (track['kind'], quote(track['material']), track['slot']))
            curves(track['curves'])
            out.write('textures ' + str(len(track['textures'])) + ''.join(' %d %s' % (frame, quote(name)) for frame, name in track['textures']) + '\n')
        out.write('visibility %d\n' % len(motion['visibility']))
        for track in motion['visibility']:
            out.write('mesh %s %d %s\n' % (quote(track['mesh']), len(track['frames']), ' '.join(str(int(v)) for v in track['frames'])))
        out.write('end\n')


def motion_sample(curve, frame, fallback):
    if not curve:
        return fallback
    if frame <= curve[0][0]:
        return curve[0][1]
    for a, b in zip(curve, curve[1:]):
        if frame <= b[0]:
            span = b[0] - a[0]
            t = (frame - a[0]) / span
            return (2*t**3-3*t*t+1)*a[1] + (t**3-2*t*t+t)*span*a[2] + (-2*t**3+3*t*t)*b[1] + (t**3-t*t)*span*b[2]
    return curve[-1][1]


def motion_curve_state(action):
    if not action:
        return []
    return [(f.data_path, f.array_index, f.extrapolation, [(tuple(k.co), k.interpolation, tuple(k.handle_left), tuple(k.handle_right)) for k in f.keyframe_points]) for f in action.fcurves]


def baked_curve(values):
    if all(abs(v-values[0]) < 1e-7 for v in values):
        return [[0, values[0], 0]]
    return [[frame, value, (values[min(frame+1, len(values)-1)]-values[max(0, frame-1)]) / (1 if frame in (0, len(values)-1) else 2)] for frame, value in enumerate(values)]


def import_motion(path, collection=None):
    if bpy.app.version >= (5, 0, 0):
        raise ValueError('Motion exchange currently requires Blender 4.2 or 4.3')
    from mathutils import Quaternion
    collection = collection or active_model_collection()
    motion = read_motion(path)
    if collection['usum_source'] != motion['model']:
        raise ValueError('Import the matching Studio model before its motion')
    bones = json.loads(collection['usum_bones'])
    if (motion['frames']+1)*max(1, len(bones)) > 2000000:
        raise ValueError('This clip is too large for a single pose-action bake')
    rig = next((o for o in collection.objects if o.get('usum_skeleton')), None)
    controller = bpy.data.objects.new(motion['name'] + ' channels', None)
    collection.objects.link(controller)
    controller['usum_motion'] = json.dumps(motion)
    controller['usum_motion_collection'] = collection.name
    controller.animation_data_create()
    control_action = bpy.data.actions.new(motion['name'] + ' material and visibility')
    control_action.use_fake_user = True
    controller.animation_data.action = control_action
    controller['usum_control_action'] = control_action.name
    tracks = {t['name']: t for t in motion['bones']}
    old_frame = bpy.context.scene.frame_current
    if rig:
        rig.data.pose_position = 'POSE'
        rig.animation_data_create()
        action = bpy.data.actions.new(motion['name'])
        action.use_fake_user = True
        rig.animation_data.action = action
        controller['usum_pose_action'] = action.name
        for frame in range(motion['frames']+1):
            bpy.context.scene.frame_set(frame)
            worlds, scales = [], []
            for bone in bones:
                bind = bone['transform']
                track = tracks.get(bone['name'])
                rotates = track and any(track['curves'][c] for c in range(3, 6))
                values = [motion_sample(track['curves'][c], frame, 0 if track['axis'] and rotates and 3 <= c < 6 else bind[c]) for c in range(9)] if track else bind
                if track and track['axis'] and any(track['curves'][c] for c in range(3, 6)):
                    axis = Vector(values[3:6])
                    rotation = Quaternion(axis.normalized(), axis.length*2) if axis.length > 1e-10 else Quaternion()
                else:
                    rotation = Euler(values[3:6], 'XYZ').to_quaternion()
                local = Matrix.LocRotScale(Vector(values[6:9]), rotation, Vector(values[:3]))
                parent = bone['parent']
                if parent >= 0:
                    if not (bone['flags'] & 2):
                        for row in range(3):
                            if abs(scales[parent][row]) > 1e-8:
                                for col in range(3):
                                    local[row][col] /= scales[parent][row]
                    world = worlds[parent] @ local
                else:
                    world = local
                worlds.append(world)
                scales.append(values[:3])
                pose = rig.pose.bones[bone['name']]
                pose.rotation_mode = 'QUATERNION'
                pose.matrix = AXES @ world
                bpy.context.view_layer.update()
                for data_path in ('location', 'rotation_quaternion', 'scale'):
                    pose.keyframe_insert(data_path=data_path, frame=frame, group=bone['name'])
        for curve in action.fcurves:
            for key in curve.keyframe_points:
                key.interpolation = 'LINEAR'
        controller['usum_pose_baseline'] = json.dumps(motion_curve_state(action))
    labels = ('Scale U', 'Scale V', 'Rotation', 'Translate U', 'Translate V')
    properties = []
    for index, track in enumerate(motion['materials']):
        if track['kind'] == 2:
            names = list(dict.fromkeys(name for _, name in track['textures']))
            key = '%d %s texture index' % (index, track['material'])
            controller[key] = 0
            controller[key + ' names'] = json.dumps(names)
            for frame, name in track['textures']:
                controller[key] = names.index(name)
                controller.keyframe_insert(data_path='[' + quote(key) + ']', frame=frame)
            properties.append([index, -1, key])
        else:
            for channel in range(4 if track['kind'] == 1 else 5):
                label = ('R', 'G', 'B', 'A')[channel] if track['kind'] == 1 else labels[channel]
                key = '%d %s [%d] %s' % (index, track['material'], track['slot'], label)
                fallback = 1 if track['kind'] == 1 or channel < 2 else 0
                controller[key] = motion_sample(track['curves'][channel], 0, fallback)
                for frame in range(motion['frames']+1):
                    controller[key] = motion_sample(track['curves'][channel], frame, fallback)
                    controller.keyframe_insert(data_path='[' + quote(key) + ']', frame=frame)
                properties.append([index, channel, key])
    mesh_names = {mesh['name'].rsplit('/', 1)[0] for mesh in json.loads(collection['usum_meshes'])}
    for index, track in enumerate(motion['visibility']):
        if track['mesh'] not in mesh_names:
            continue
        key = 'Visible ' + track['mesh']
        controller[key] = True
        for frame, visible in enumerate(track['frames']):
            controller[key] = bool(visible)
            controller.keyframe_insert(data_path='[' + quote(key) + ']', frame=frame)
        for obj in collection.objects:
            if 'usum_mesh' not in obj:
                continue
            meta = json.loads(collection['usum_meshes'])[obj['usum_mesh']]
            if meta['name'].rsplit('/', 1)[0] != track['mesh']:
                continue
            for field in ('hide_viewport', 'hide_render'):
                obj.driver_remove(field)
                driver = obj.driver_add(field).driver
                variable = driver.variables.new()
                variable.name = 'visible'
                variable.targets[0].id = controller
                variable.targets[0].data_path = '[' + quote(key) + ']'
                driver.expression = 'visible < 0.5'
    for curve in control_action.fcurves:
        for key in curve.keyframe_points:
            key.interpolation = 'CONSTANT' if 'Visible ' in curve.data_path or 'texture index' in curve.data_path else 'LINEAR'
    controller['usum_properties'] = json.dumps(properties)
    controller['usum_control_baseline'] = json.dumps(motion_curve_state(control_action))
    collection['usum_active_motion'] = controller.name
    scene = bpy.context.scene
    scene.render.fps, scene.render.fps_base = 30, 1
    scene.frame_start, scene.frame_end = 0, motion['frames']
    scene.frame_set(0)
    bpy.ops.object.select_all(action='DESELECT')
    target = rig or controller
    target.select_set(True)
    bpy.context.view_layer.objects.active = target
    return controller


def export_motion(path, collection=None):
    if bpy.app.version >= (5, 0, 0):
        raise ValueError('Motion exchange currently requires Blender 4.2 or 4.3')
    collection = collection or active_model_collection()
    controller = bpy.data.objects.get(collection.get('usum_active_motion', ''))
    if controller is None:
        raise ValueError('Import a Studio motion into this model first')
    motion = json.loads(controller['usum_motion'])
    rig = next((o for o in collection.objects if o.get('usum_skeleton')), None)
    bones = json.loads(collection['usum_bones'])
    action = rig.animation_data.action if rig and rig.animation_data else None
    pose_changed = rig and json.dumps(motion_curve_state(action)) != controller['usum_pose_baseline']
    old_frame = bpy.context.scene.frame_current
    try:
        if pose_changed:
            if not action:
                raise ValueError('Assign a pose action before exporting the edited motion')
            samples = {b['name']: [[] for _ in range(9)] for b in bones}
            previous = {}
            for frame in range(motion['frames']+1):
                bpy.context.scene.frame_set(frame)
                worlds, scales = [], []
                for bone in bones:
                    world = INVERSE_AXES @ rig.pose.bones[bone['name']].matrix
                    parent = bone['parent']
                    local = worlds[parent].inverted() @ world if parent >= 0 else world.copy()
                    if parent >= 0 and not (bone['flags'] & 2):
                        for row in range(3):
                            for col in range(3):
                                local[row][col] *= scales[parent][row]
                    location, rotation, scale = local.decompose()
                    angles = rotation.to_euler('XYZ', previous[bone['name']]) if bone['name'] in previous else rotation.to_euler('XYZ')
                    previous[bone['name']] = angles.copy()
                    worlds.append(world)
                    scales.append(scale)
                    for channel, value in enumerate(tuple(scale) + tuple(angles) + tuple(location)):
                        samples[bone['name']][channel].append(value)
            known = {b['name'] for b in bones}
            preserved = [t for t in motion['bones'] if t['name'] not in known]
            from mathutils import Quaternion
            originals = {t['name']: t for t in motion['bones']}
            result = preserved
            for bone in bones:
                old = originals.get(bone['name'])
                values = samples[bone['name']]
                track = json.loads(json.dumps(old)) if old else {'name': bone['name'], 'axis': 0, 'curves': [[] for _ in range(9)]}
                changed = False
                for channel in (0, 1, 2, 6, 7, 8):
                    curve = old['curves'][channel] if old else []
                    if any(abs(value-motion_sample(curve, frame, bone['transform'][channel])) > .0001 for frame, value in enumerate(values[channel])):
                        track['curves'][channel] = baked_curve(values[channel])
                        changed = True
                rotation_changed = False
                for frame in range(motion['frames']+1):
                    rotated = old and any(old['curves'][c] for c in range(3, 6))
                    base = [motion_sample(old['curves'][c] if old else [], frame, 0 if old and old['axis'] and rotated else bone['transform'][c]) for c in range(3, 6)]
                    if old and old['axis'] and rotated:
                        axis = Vector(base)
                        expected = Quaternion(axis.normalized(), axis.length*2) if axis.length > 1e-10 else Quaternion()
                    else:
                        expected = Euler(base, 'XYZ').to_quaternion()
                    actual = Euler([values[c][frame] for c in range(3, 6)], 'XYZ').to_quaternion()
                    if abs(expected.dot(actual)) < .9999999:
                        rotation_changed = True
                        break
                if rotation_changed:
                    track['axis'] = 0
                    for channel in range(3, 6):
                        track['curves'][channel] = baked_curve(values[channel])
                    changed = True
                if changed or old:
                    result.append(track)
            motion['bones'] = result
        action = controller.animation_data.action
        baseline = {entry[0]: entry for entry in json.loads(controller['usum_control_baseline'])}
        current = {entry[0]: entry for entry in json.loads(json.dumps(motion_curve_state(action)))}
        def changed(key):
            data_path = '[' + quote(key) + ']'
            return current.get(data_path) != baseline.get(data_path)
        def samples(key):
            result = []
            for frame in range(motion['frames']+1):
                bpy.context.scene.frame_set(frame)
                result.append(float(controller.evaluated_get(bpy.context.evaluated_depsgraph_get())[key]))
            return result
        for index, channel, key in json.loads(controller['usum_properties']):
            if not changed(key):
                continue
            track = motion['materials'][index]
            values = samples(key)
            if channel >= 0:
                track['curves'][channel] = baked_curve(values)
            else:
                names = json.loads(controller[key + ' names'])
                keys = []
                for frame, value in enumerate(values):
                    index = round(value)
                    if index < 0 or index >= len(names):
                        raise ValueError('Texture pattern index is outside its names list')
                    if not keys or names[index] != keys[-1][1]:
                        keys.append([frame, names[index]])
                track['textures'] = keys
        for track in motion['visibility']:
            key = 'Visible ' + track['mesh']
            if changed(key):
                track['frames'] = [int(value >= .5) for value in samples(key)]
    finally:
        bpy.context.scene.frame_set(old_frame)
    write_motion(path, motion)
    return motion


class USUM_OT_motion_channels(bpy.types.Operator):
    bl_idname = 'object.usum_motion_channels'
    bl_label = 'Select material and visibility channels'
    def execute(self, context):
        collection = active_model_collection()
        controller = bpy.data.objects[collection['usum_active_motion']]
        bpy.ops.object.select_all(action='DESELECT')
        controller.select_set(True)
        context.view_layer.objects.active = controller
        return {'FINISHED'}


class USUM_PT_motion(bpy.types.Panel):
    bl_label = 'Studio motion'
    bl_idname = 'USUM_PT_motion'
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'USUM'
    def draw(self, context):
        try:
            collection = active_model_collection()
            controller = bpy.data.objects.get(collection.get('usum_active_motion', ''))
        except ValueError:
            controller = None
        if controller is None:
            self.layout.label(text='Import a Studio model and motion first.')
            return
        motion = json.loads(controller['usum_motion'])
        self.layout.label(text=motion['name'])
        self.layout.label(text='30 fps | Frames 0–%d | %s' % (motion['frames'], 'Loop' if motion['looping'] else 'Once'))
        self.layout.operator(USUM_OT_motion_channels.bl_idname)
        self.layout.label(text='Edit pose keys in the Action Editor.')
        self.layout.label(text='Channel keys use the Graph Editor.')
        for _, _, key in json.loads(controller['usum_properties']):
            self.layout.prop(controller, '[' + quote(key) + ']', text=key)
        for track in motion['visibility']:
            key = 'Visible ' + track['mesh']
            if key in controller:
                self.layout.prop(controller, '[' + quote(key) + ']', text=key)


class USUM_OT_import_motion(bpy.types.Operator, ImportHelper):
    bl_idname = 'import_scene.usum_motion'
    bl_label = 'Import USUM Studio motion'
    bl_options = {'UNDO'}
    filename_ext = '.usum-motion'
    filter_glob: StringProperty(default='*.usum-motion', options={'HIDDEN'})
    def execute(self, context):
        try:
            import_motion(self.filepath)
            return {'FINISHED'}
        except Exception as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class USUM_OT_export_motion(bpy.types.Operator, ExportHelper):
    bl_idname = 'export_scene.usum_motion'
    bl_label = 'Export USUM Studio motion'
    filename_ext = '.usum-motion'
    filter_glob: StringProperty(default='*.usum-motion', options={'HIDDEN'})
    def execute(self, context):
        try:
            export_motion(self.filepath)
            return {'FINISHED'}
        except Exception as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class USUM_OT_import_model(bpy.types.Operator, ImportHelper):
    bl_idname = "import_scene.usum_model"
    bl_label = "Import USUM Studio model"
    bl_options = {'UNDO'}
    filename_ext = ".usum-model"
    filter_glob: StringProperty(default="*.usum-model", options={'HIDDEN'})
    def execute(self, context):
        try:
            import_model(self.filepath)
            return {'FINISHED'}
        except Exception as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class USUM_OT_export_model(bpy.types.Operator, ExportHelper):
    bl_idname = "export_scene.usum_model"
    bl_label = "Export USUM Studio model"
    filename_ext = ".usum-model"
    filter_glob: StringProperty(default="*.usum-model", options={'HIDDEN'})
    def execute(self, context):
        try:
            export_model(self.filepath)
            return {'FINISHED'}
        except Exception as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


def import_menu(self, context):
    self.layout.operator(USUM_OT_import_motion.bl_idname, text="USUM Studio motion (.usum-motion)")
    self.layout.operator(USUM_OT_import_model.bl_idname, text="USUM Studio model (.usum-model)")


def export_menu(self, context):
    self.layout.operator(USUM_OT_export_motion.bl_idname, text="USUM Studio motion (.usum-motion)")
    self.layout.operator(USUM_OT_export_model.bl_idname, text="USUM Studio model (.usum-model)")


def register():
    bpy.utils.register_class(USUM_OT_motion_channels)
    bpy.utils.register_class(USUM_PT_motion)
    bpy.utils.register_class(USUM_OT_import_motion)
    bpy.utils.register_class(USUM_OT_export_motion)
    bpy.utils.register_class(USUM_OT_import_model)
    bpy.utils.register_class(USUM_OT_export_model)
    bpy.types.TOPBAR_MT_file_import.append(import_menu)
    bpy.types.TOPBAR_MT_file_export.append(export_menu)


def unregister():
    bpy.utils.unregister_class(USUM_PT_motion)
    bpy.utils.unregister_class(USUM_OT_motion_channels)
    bpy.utils.unregister_class(USUM_OT_export_motion)
    bpy.utils.unregister_class(USUM_OT_import_motion)
    bpy.types.TOPBAR_MT_file_export.remove(export_menu)
    bpy.types.TOPBAR_MT_file_import.remove(import_menu)
    bpy.utils.unregister_class(USUM_OT_export_model)
    bpy.utils.unregister_class(USUM_OT_import_model)


if __name__ == "__main__":
    register()
