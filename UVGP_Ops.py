import bpy
import ctypes
import bmesh
from bpy.app.handlers import persistent

UvgpLib = ctypes.cdll.LoadLibrary("T:/workshop_folders/UVGP/Win64/UVGP.dll")
#UvgpLib = ctypes.cdll.LoadLibrary("T:\workshop_folders/UVGPWin/UVGP/x64/Debug/UVGP.dll")

class Vec3(ctypes.Structure):
        _fields_ = [("x", ctypes.c_float),
                    ("y", ctypes.c_float),
                    ("z", ctypes.c_float)]

class Vert(ctypes.Structure):
    _fields_ = [("pos", Vec3)] 

class Loop(ctypes.Structure):
    _fields_ = [("vert", ctypes.c_int),
                ("normal", Vec3)]

class Face(ctypes.Structure):
    _fields_ = [("loopAmount", ctypes.c_int),
                ("loops", Loop * 4)]

class UVGP_OT_UvgpExportUvgpFile(bpy.types.Operator):
    bl_idname = "uvgp.uvgp_export_uvgp_file"
    bl_label = "UVGP Export"
    bl_options = {'REGISTER'}

    def execute(self, context):
        print("Piss")
        if (len(context.selected_objects) == 0):
            print("UVGP export failed, no objects selected.")
            return {'CANCELLED'}
        if (len(context.selected_objects) > 1):
            print("UVGP export failed, more than one object selected.")
            return {'CANCELLED'}
        obj = context.selected_objects[0]
        depsgraph = context.evaluated_depsgraph_get()
        objEval = obj.evaluated_get(depsgraph)
        meshEval = objEval.data

        bMeshEval = bmesh.new()
        bMeshEval.from_mesh(meshEval)

        faceAmount = len(bMeshEval.faces)
        vertAmount = len(bMeshEval.verts)
        FaceBufferType = Face * faceAmount
        VertBufferType = Vert * vertAmount
        faceBuffer = FaceBufferType()
        vertBuffer = VertBufferType()

        for vert in bMeshEval.verts:
            vertBuffer[vert.index].pos.x = vert.co.x
            vertBuffer[vert.index].pos.y = vert.co.y
            vertBuffer[vert.index].pos.z = vert.co.z
        
        for face in bMeshEval.faces:
            loopIndex = 0
            faceBuffer[face.index].loopAmount = len(face.loops)
            for loop in face.loops:
                faceBuffer[face.index].loops[loopIndex].vert = loop.vert.index
                loopNormal = loop.calc_normal()
                faceBuffer[face.index].loops[loopIndex].normal.x = loopNormal.x
                faceBuffer[face.index].loops[loopIndex].normal.y = loopNormal.y
                faceBuffer[face.index].loops[loopIndex].normal.z = loopNormal.z
                loopIndex += 1
        
        UvgpLib.UvgpExportUvgpFile(vertAmount, ctypes.pointer(vertBuffer), faceAmount, ctypes.pointer(faceBuffer))

        return {'FINISHED'}

class UVGP_OT_UvgpUpdate(bpy.types.Operator):
    bl_idname = "uvgp.uvgp_update"
    bl_label = "UVGP Update"

    def __init__(self):
        print("Initializing UVGP")

    def __del__(self):
        print("Ending UVGP")

    def execute(self, context):
        return {'FINISHED'}

    def modal(self, context, event):
        print("Updating UVGP")
        return {'RUNNING_MODAL'}

    def invoke(self, context, event):
        return {'RUNNING_MODAL'}

class UVGP_OT_UvgpAssign(bpy.types.Operator):
    bl_idname = "uvgp.uvgp_assign"
    bl_label = "UVGP Assign"
    bl_options = {'REGISTER'}

    def execute(self, context):
        uvgp = context.scene.uvgp
        for obj in context.selected_objects:
            exists = False
            for target in context.scene.uvgpTargets:
                if target.obj == obj:
                    exists = True
                    break
            if exists:
                continue
            newTarget = context.scene.uvgpTargets.add()
            newTarget.obj = obj.id_data
            newTarget.id = uvgp.nextTargetId
            obj.uvgpTargetId = uvgp.nextTargetId
            uvgp.nextTargetId += 1
        return {'FINISHED'}

@persistent
def uvgpDepsgraphUpdatePostHandler(dummy):
    pass

classes = [UVGP_OT_UvgpExportUvgpFile,
           UVGP_OT_UvgpUpdate,
           UVGP_OT_UvgpAssign]

#Register
def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.app.handlers.depsgraph_update_post.append(uvgpDepsgraphUpdatePostHandler)

#Unregister
def unregister():
    for cls in classes:
        bpy.utils.unregister_class(cls)
    bpy.app.handlers.depsgraph_update_post.remove(uvgpDepsgraphUpdatePostHandler)
