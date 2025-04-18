bl_info = {
    "name": "Defold Mesh Binary Export",
    "author": "",
    "version": (2, 5),
    "blender": (3, 0, 0),
    "location": "File > Export > Defold Binary Mesh (.bin)",
    "description": "Export to Defold .mesh format",
    "warning": "",
    "wiki_url": "",
    "tracker_url": "",
    "category": "Import-Export"
}

import os, bpy, struct, mathutils
from bpy_extras.io_utils import ExportHelper
from bpy.props import StringProperty, BoolProperty
from bpy_types import Operator
from pathlib import Path


def write_shape_values(mesh, shapes, f, ff):
    for shape in shapes:
        s = mesh.shape_keys.key_blocks.get(shape['name'])
        value = float("{:.2f}".format(s.value))
        f.write(struct.pack('i', len(shape['name'])))
        f.write(bytes(shape['name'], "ascii"))
        f.write(struct.pack(ff, value))


def sort_weights(vg):
    return -vg.weight


def set_frame(context, frame):
    context.scene.frame_set(frame)
    # there is an issue with IK bones (and maybe other simulations?)
    # looks like Blender needs some time to compute it
    context.scene.frame_set(frame)  # twice to make sure IK computations is done?
    context.view_layer.update()  # not enough?
    depsgraph = context.evaluated_depsgraph_get()


def get_armature_by_name(armatures, name: str):
    for armature in armatures:
        if armature['name'] == name:
            return armature


def find_objects_and_armatures(context, export_hidden_settings: bool):
    armatures = []
    objects = []
    # ---------------------find-armatures-----------------------
    index = 0
    for obj in context.scene.objects:
        if obj.type != 'MESH':
            continue
        if obj.mode == 'EDIT':
            obj.mode_set(mode='OBJECT', toggle=False)
        mesh = obj.data
        mesh.calc_loop_triangles()
        mesh.calc_normals_split()
        if len(mesh.loop_triangles) == 0:
            continue
        if not obj.visible_get() and not export_hidden_settings:
            continue
        objects.append(obj)
        armature = obj.find_armature()
        if armature and (not get_armature_by_name(armatures, armature.name)):
            armatures.append({'armature': armature, 'bones': [], 'name': armature.name, 'index': index})
            index = index + 1
    # ---------------------optimize-armatures--------------------
    # sometimes armatures contain a lot of not used bones!
    for proxy in armatures:
        bones_map = {bone.name: False for bone in proxy['armature'].pose.bones}

        def add_parent(bone):
            if bone.parent:
                bones_map[bone.parent.name] = True
                add_parent(bone.parent)

        for obj in objects:
            if obj.find_armature() != proxy['armature']:
                continue
            for vert in obj.data.vertices:
                fixed_groups = []
                for wgrp in vert.groups:
                    group = obj.vertex_groups[wgrp.group]
                    if wgrp.weight > 0 and group.name in bones_map:
                        fixed_groups.append(wgrp)
                        # bones_map[group.name] = True
                fixed_groups.sort(key=sort_weights)
                fixed_groups = fixed_groups[:4]
                for wgrp in fixed_groups:
                    group = obj.vertex_groups[wgrp.group]
                    bones_map[group.name] = True
        for bone in proxy['armature'].pose.bones:
            if bones_map[bone.name]:
                add_parent(bone)
        for bone in proxy['armature'].pose.bones:
            if bones_map[bone.name]:
                proxy['bones'].append(bone)
    return {'armatures': armatures, 'objects': objects}


def write_matrix(file, float_format, matrix):
    file.write(struct.pack(float_format, *matrix[0]))
    file.write(struct.pack(float_format, *matrix[1]))
    file.write(struct.pack(float_format, *matrix[2]))


def write_armatures(file, context, armatures, float_format, export_anim: bool):
    file.write(struct.pack('i', len(armatures)))
    f4 = float_format * 4
    for proxy in armatures:
        file.write(struct.pack('i', len(proxy['bones'])))
        for pbone in proxy['bones']:
            file.write(struct.pack('i', len(pbone.name)))
            file.write(bytes(pbone.name, "ascii"))
            parent = -1
            for idx, b in enumerate(proxy['bones']):
                if b == pbone.parent:
                    parent = idx
                    break
            file.write(struct.pack('i', parent))
            write_matrix(file, f4, pbone.bone.matrix_local)
        if export_anim:
            file.write(struct.pack('i', context.scene.frame_end - 1))
            current_frame = context.scene.frame_current
            for frame in range(1, context.scene.frame_end):
                set_frame(context, frame)
                for pbone in proxy['bones']:
                    write_matrix(file, f4, pbone.matrix_basis)
                set_frame(context, current_frame)  # as we work with object's global matrix in particular frame
        else:
            file.write(struct.pack('i', 1))  # single frame flag
            for pbone in proxy['bones']:
                write_matrix(file, f4, pbone.matrix_basis)


def find_nodes_to(socket, node_type):
    for link in socket.links:
        node = link.from_node
        if node.bl_static_type == node_type:
            return [node]
        else:
            for node_input in node.inputs:
                n = find_nodes_to(node_input, node_type)
                if n:
                    n.append(node)
                    return n
    return None


def find_node(socket, node_type):
    nodes = find_nodes_to(socket, node_type)
    if nodes:
        # print([n.bl_static_type for n in nodes])
        return nodes[0]
    return None


def find_texture(socket):
    tex_node = find_node(socket, 'TEX_IMAGE')
    if tex_node is not None:
        texture = Path(tex_node.image.filepath).name
        return texture
    return None


def find_ramp(socket):
    node = find_node(socket, 'VALTORGB')
    if node and node.color_ramp:
        ramp = node.color_ramp
        e1 = ramp.elements[0]
        e2 = ramp.elements[len(ramp.elements) - 1]
        return {'p1': e1.position, 'v1': e1.color[0], 'p2': e2.position, 'v2': e2.color[0]}
        # simplified ramp, just intensity of first and last element, enough for specular\roughness


def write_models(file, context, armatures, objects, float_format, export_anim):
    f3 = float_format * 3
    for obj in objects:
        print(obj.name)
        mesh = obj.data
        file.write(struct.pack('i', len(obj.name)))
        file.write(bytes(obj.name, "ascii"))
        if obj.parent:
            file.write(struct.pack('i', len(obj.parent.name)))
            file.write(bytes(obj.parent.name, "ascii"))
        else:
            file.write(struct.pack('i', 0))  # no parent flag
        t = obj.matrix_world @ obj.matrix_local.inverted()  # to remove local transfrom - we will apply it to vertices
        (translation, rotation, scale) = t.decompose()
        file.write(struct.pack(f3, *translation))
        file.write(struct.pack(f3, *rotation.to_euler()))
        file.write(struct.pack(f3, *scale))

        # ---------------------read-materials-----------------------
        materials = []
        for m in mesh.materials:
            # print("--------------------------")
            # print("material", m.name, m.blend_method)
            if m is None:  # create default material
                material = {'name': 'default', 'col': [0.7, 0.7, 0.7, 1], 'method': 'OPAQUE', 'spec_power': 0.0,
                            'roughness': 0.0, 'normal_strength': 1.0}
                materials.append(material)
                continue
            material = {'name': m.name, 'col': m.diffuse_color, 'method': m.blend_method, 'spec_power': 0.0,
                        'roughness': 0.0, 'normal_strength': 1.0}
            materials.append(material)
            if m.node_tree:
                for principled in m.node_tree.nodes:
                    if principled.bl_static_type != 'BSDF_PRINCIPLED':
                        continue
                    specular = principled.inputs['Specular']
                    material['spec_power'] = specular.default_value
                    material['specular'] = find_texture(specular)
                    if material.get('specular'):
                        material['specular_invert'] = 1 if find_node(specular, 'INVERT') else 0
                        material['specular_ramp'] = find_ramp(specular)
                    roughness = principled.inputs['Roughness']
                    material['roughness'] = roughness.default_value
                    material['roughness_tex'] = find_texture(roughness)
                    material['roughness_ramp'] = find_ramp(roughness)
                    normal_map = find_node(principled.inputs['Normal'], 'NORMAL_MAP')
                    if normal_map:
                        material['normal'] = find_texture(normal_map.inputs['Color'])
                        material['normal_strength'] = normal_map.inputs['Strength'].default_value
                    else:  # Look in the group
                        group = find_node(principled.inputs['Normal'], 'GROUP')
                        if group:
                            normal_map = group.inputs.get("Normal Map")
                            if normal_map:
                                material['normal'] = find_texture(normal_map)
                                str = group.inputs.get('Normal Strength')
                                material['normal_strength'] = str and str.default_value or 1.
                    base_color = principled.inputs['Base Color']
                    value = base_color.default_value
                    is_subsurface = principled.inputs['Subsurface'].default_value == 1.
                    material['col'] = [value[0], value[1], value[2], principled.inputs['Alpha'].default_value]
                    if is_subsurface:
                        material['texture'] = find_texture(principled.inputs['Subsurface Color'])
                    else:
                        material['texture'] = find_texture(base_color)
                    if material.get('texture') is not None:
                        # print(material['texture'])
                        break  # TODO objects with combined shaders

        if len(materials) == 0:  # create default material
            material = {'name': 'default', 'col': [0.7, 0.7, 0.7, 1], 'method': 'OPAQUE', 'spec_power': 0.0,
                        'roughness': 0.0, 'normal_strength': 1.0}
            materials.append(material)

        # ---------------------write-geometry------------------------
        file.write(struct.pack('i', len(mesh.vertices)))
        for vert in mesh.vertices:
            v = obj.matrix_local @ vert.co  # apply local transform,
            # or we have to deal with it calculating bones
            file.write(struct.pack(f3, *v))
            v = (obj.matrix_local @ vert.normal).normalized()
            file.write(struct.pack(f3, *v))
        shapes = []
        if mesh.shape_keys and len(mesh.shape_keys.key_blocks) > 0:
            for shape in mesh.shape_keys.key_blocks:
                s = {'name': shape.name, 'deltas': [], 'value': shape.value}
                normals = shape.normals_vertex_get()
                for i in range(len(shape.data)):
                    vert = mesh.vertices[i]
                    if (shape.data[i].co - vert.co).length > 0.001:
                        delta_pos = (obj.matrix_local @ shape.data[i].co) - (obj.matrix_local @ vert.co)
                        s['deltas'].append({'idx': i, 'p': delta_pos, 'n': obj.matrix_local @ mathutils.Vector(
                            (normals[i * 3] - vert.normal.x,
                             normals[i * 3 + 1] - vert.normal.y,
                             normals[i * 3 + 2] - vert.normal.z))})
                if len(s['deltas']) > 0:
                    shapes.append(s)
        file.write(struct.pack('i', len(shapes)))
        for shape in shapes:
            file.write(struct.pack('i', len(shape['name'])))
            file.write(bytes(shape['name'], "ascii"))
            file.write(struct.pack('i', len(shape['deltas'])))
            for vert in shape['deltas']:
                file.write(struct.pack('i', vert['idx']))
                file.write(struct.pack(f3, *vert['p']))
                file.write(struct.pack(f3, *vert['n']))

        file.write(struct.pack('i', len(mesh.loop_triangles)))
        uv = []
        for face in mesh.loop_triangles:
            file.write(struct.pack('iii', *face.vertices))
            file.write(struct.pack('i', face.material_index))
            for loop_idx in face.loops:
                uv_cords = mesh.uv_layers.active.data[loop_idx].uv if mesh.uv_layers.active else (0, 0)
                uv.extend(uv_cords)
            if not face.use_smooth:
                file.write(struct.pack('i', 1))
                v = (obj.matrix_local @ face.normal).normalized()
                file.write(struct.pack(f3, *v))
            else:
                file.write(struct.pack('i', 0))

        file.write(struct.pack(float_format * len(uv), *uv))
        file.write(struct.pack('i', len(materials)))

        # ---------------------write-materials------------------------
        for material in materials:
            print("MATERIAL " + material['name'])
            file.write(struct.pack('i', len(material['name'])))
            file.write(bytes(material['name'], "ascii"))
            method = 1
            if material['method'] == 'OPAQUE':
                method = 0
            elif material['method'] == 'HASHED':
                method = 2
            file.write(struct.pack('i', method))
            file.write(struct.pack(float_format * 4, *material['col']))
            file.write(struct.pack(float_format, material['spec_power']))
            file.write(struct.pack(float_format, material['roughness']))
            if material.get('texture') is None:
                file.write(struct.pack('i', 0))  # no texture flag
            else:
                file.write(struct.pack('i', len(material['texture'])))
                file.write(bytes(material['texture'], "ascii"))
            if material.get('normal') is None:
                file.write(struct.pack('i', 0))  # no normal texture flag
            else:
                file.write(struct.pack('i', len(material['normal'])))
                file.write(bytes(material['normal'], "ascii"))
                file.write(struct.pack(float_format, material['normal_strength']))
            if material.get('specular') is None:
                file.write(struct.pack('i', 0))  # no texture flag
            else:
                file.write(struct.pack('i', len(material['specular'])))
                file.write(bytes(material['specular'], "ascii"))
                file.write(struct.pack('i', material['specular_invert']))
                if material.get('specular_ramp') is None:
                    file.write(struct.pack('i', 0))  # no ramp flag
                else:
                    file.write(struct.pack('i', 1))
                    file.write(struct.pack(float_format, material['specular_ramp']['p1']))
                    file.write(struct.pack(float_format, material['specular_ramp']['v1']))
                    file.write(struct.pack(float_format, material['specular_ramp']['p2']))
                    file.write(struct.pack(float_format, material['specular_ramp']['v2']))
            if material.get('roughness_tex') is None:
                file.write(struct.pack('i', 0))  # no roughbess texture flag
            else:
                file.write(struct.pack('i', len(material['roughness_tex'])))
                file.write(bytes(material['roughness_tex'], "ascii"))
                if material.get('roughness_ramp') is None:
                    file.write(struct.pack('i', 0))  # no ramp flag
                else:
                    file.write(struct.pack('i', 1))
                    file.write(struct.pack(float_format, material['roughness_ramp']['p1']))
                    file.write(struct.pack(float_format, material['roughness_ramp']['v1']))
                    file.write(struct.pack(float_format, material['roughness_ramp']['p2']))
                    file.write(struct.pack(float_format, material['roughness_ramp']['v2']))
        # f.close()
        # return {'FINISHED'}

        # ---------------------write-bones------------------------
        armature = obj.find_armature()
        max_bones_per_vertex = 4
        if armature:
            # pose = armature.pose
            proxy = get_armature_by_name(armatures, armature.name)
            file.write(struct.pack('i', proxy['index']))  # id of saved armature
            bones_map = {bone.name: i for i, bone in enumerate(proxy['bones'])}
            for vert in mesh.vertices:
                fixed_groups = []
                for wgrp in vert.groups:
                    group = obj.vertex_groups[wgrp.group]
                    if wgrp.weight > 0 and group.name in bones_map:
                        fixed_groups.append(wgrp)
                fixed_groups.sort(key=sort_weights)
                fixed_groups = fixed_groups[:max_bones_per_vertex]
                total = 0
                for vg in fixed_groups:
                    total = total + vg.weight
                for vg in fixed_groups:
                    vg.weight = vg.weight / total

                file.write(struct.pack('i', len(fixed_groups)))
                for wgrp in fixed_groups:
                    group = obj.vertex_groups[wgrp.group]
                    bone_idx = bones_map[group.name]
                    file.write(struct.pack('i', bone_idx))
                    file.write(struct.pack(float_format, wgrp.weight))
            # f.write(struct.pack('i', 1 if export_precompute_setting else 0))
        else:
            file.write(struct.pack('i', -1))  # no bones flag
        if export_anim and len(shapes) > 0:
            file.write(struct.pack('i', context.scene.frame_end - 1))
            current_frame = context.scene.frame_current
            for frame in range(1, context.scene.frame_end):
                set_frame(context, frame)
                write_shape_values(mesh, shapes, file, float_format)
            set_frame(context, current_frame)  # as we work with object's global matrix in particular frame
        elif len(shapes) > 0:
            file.write(struct.pack('i', 1))  # single frame flag
            write_shape_values(mesh, shapes, file, float_format)
        else:
            file.write(struct.pack('i', 0))  # no shape animations


# ExportHelper is a helper class, defines filename and
# invoke() function which calls the file selector.
class DefoldExport(Operator, ExportHelper):
    """This appears in the tooltip of the operator and in the generated docs"""
    bl_idname = "export_mesh.defold_binary"  # important since its how bpy.ops.import_test.some_data is constructed
    bl_label = "Export binary"
    bl_description = "Export Defold binary mesh data"

    # ExportHelper mixin class uses this
    filename_ext = ".bin"
    filter_glob: StringProperty(
        default="*.bin",
        options={'HIDDEN'},
        maxlen=255,  # Max internal buffer length, longer would be clamped.
    )
    export_anim: BoolProperty(
        name="Export animations",
        description="Only for armatures",
        default=False,
    )
    export_hidden: BoolProperty(
        name="Export hidden meshes",
        description="",
        default=False,
    )
    export_halfprecision: BoolProperty(
        name="Half precision floats",
        description="2 bytes float numbers",
        default=False,
    )
    export_separate_animations: BoolProperty(
        name="Separate animations",
        description="Export animations to separate files",
        default=True,
    )

    def execute(self, context):
        data = find_objects_and_armatures(context, self.export_hidden)
        armatures = data['armatures']
        objects = data['objects']
        export_animations = self.export_anim
        if self.export_separate_animations:
            export_animations = False
        file = open(self.filepath, 'wb')
        file.write(struct.pack('i', 1 if self.export_halfprecision else 0))
        float_format = 'e' if self.export_halfprecision else 'f'
        write_armatures(file, context, armatures, float_format, export_animations)
        write_models(file, context, armatures, objects, float_format, export_animations)
        file.close()
        if self.export_separate_animations and self.export_anim:
            for proxy in armatures:
                armature = proxy['armature']
                animation_file_name = bpy.path.clean_name(armature.animation_data.action.name) + ".bin"
                file_name = os.path.join(os.path.dirname(self.filepath), animation_file_name)
                print("Export animation:", file_name)
                file = open(file_name, 'wb')
                file.write(struct.pack('i', 1 if self.export_halfprecision else 0))
                write_armatures(file, context, [proxy], float_format, True)
                file.close()
        return {'FINISHED'}


# Only needed if you want to add into a dynamic menu
def menu_func_export(self, context):
    self.layout.operator(DefoldExport.bl_idname, text="Defold Binary Mesh (.bin)")


# Register and add to the "file selector" menu (required to use F3 search "Text Export Operator" for quick access)
def register():
    bpy.utils.register_class(DefoldExport)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)


def unregister():
    bpy.utils.unregister_class(DefoldExport)
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)


if __name__ == "__main__":
    register()

    # test call
    bpy.ops.export_mesh.defold_binary('INVOKE_DEFAULT')
