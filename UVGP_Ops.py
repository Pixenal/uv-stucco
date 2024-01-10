import bpy
import ctypes
import bmesh
from bpy.app.handlers import persistent

#uvgpLib = ctypes.cdll.LoadLibrary("T:/workshop_folders/UVGP/Win64/UVGP.dll")
uvgpLib = ctypes.cdll.LoadLibrary("/run/media/calebdawson/Tuna/workshop_folders/UVGP/Linux/UVGP.so")
#uvgpLib = ctypes.cdll.LoadLibrary("T:\workshop_folders/UVGPWin/UVGP/x64/Debug/UVGP.dll")

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

class BlenderVert(ctypes.Structure):
    _fields_ = [("x", ctypes.c_float),
                ("y", ctypes.c_float),
                ("z", ctypes.c_float)]

class BlenderUv(ctypes.Structure):
    _fields_ = [("u", ctypes.c_float),
                ("v", ctypes.c_float)]

class BlenderMeshData(ctypes.Structure):
    _fields_ = [("vertAmount", ctypes.c_int),
                ("vertBuffer", ctypes.POINTER(BlenderVert)),
                ("loopAmount", ctypes.c_int),
                ("loopBuffer", ctypes.POINTER(ctypes.c_int)),
                ("faceAmount", ctypes.c_int),
                ("faceBuffer", ctypes.POINTER(ctypes.c_int)),
                ("uvBuffer", ctypes.POINTER(BlenderUv))]

class UVGP_OT_UvgpExportUvgpFile(bpy.types.Operator):
    bl_idname = "uvgp.uvgp_export_uvgp_file"
    bl_label = "UVGP Export"
    bl_options = {'REGISTER'}

    def execute(self, context):
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

        faceAmount = len(meshEval.polygons)
        loopAmount = len(meshEval.loops)
        vertAmount = len(meshEval.vertices)
        vertsPtr = meshEval.vertices[0].as_pointer()
        vertsPtrFloat = ctypes.cast(vertsPtr, ctypes.POINTER(ctypes.c_float))
        loopsPtr = meshEval.loops[0].as_pointer()
        loopsPtrInt = ctypes.cast(loopsPtr, ctypes.POINTER(ctypes.c_int))
        facesPtr = meshEval.polygons[0].as_pointer()
        facesPtrInt = ctypes.cast(facesPtr, ctypes.POINTER(ctypes.c_int))
        
        uvgpLib.UvgpExportUvgpFile(vertAmount, vertsPtrFloat, loopAmount, loopsPtrInt, faceAmount, facesPtrInt)

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
        if len(context.selected_objects) == 0:
            return {'CANCELLED'}
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

class UVGP_OT_UvgpLoadUvgpFile(bpy.types.Operator):
    bl_idname = "uvgp.load_uvgp_file"
    bl_label = "Load UVGP File"
    bl_options = {"REGISTER"}

    def execute(self, context):
        filePath = "/run/media/calebdawson/Tuna/workshop_folders/UVGP/TestOutputDir/File.uvgp"
        filePathUtf8 = filePath.encode('utf-8')
        uvgpLib.uvgpLoadUvgpFile(filePathUtf8)
        return {'FINISHED'}

class UVGP_OT_UvgpRemove(bpy.types.Operator):
    bl_idname = "uvgp.uvgp_remove"
    bl_label = "UVGP Remove"
    bl_options = {"REGISTER"}

    def execute(self, context):
        scene = context.scene
        if scene.uvgpTargetsIndex >= len(scene.uvgpTargets):
            return {'CANCELLED'}
        del scene.uvgpTargets[scene.uvgpTargetsIndex].obj["uvgpTargetId"]
        scene.uvgpTargets.remove(scene.uvgpTargetsIndex)
        return {'FINISHED'}

@persistent
def uvgpDepsgraphUpdatePostHandler(dummy):
    scene = bpy.context.scene
    depsgraph = bpy.context.evaluated_depsgraph_get()
    for target in scene.uvgpTargets:
        obj = target.obj;
        if not(obj in bpy.context.selected_objects):
            continue
        elif obj.mode != 'OBJECT':
            continue
        mesh = obj.data
        objEval = obj.evaluated_get(depsgraph)
        meshEval = objEval.data

        mesh = BlenderMeshData()
        mesh.faceAmount = len(meshEval.polygons)
        mesh.loopAmount = len(meshEval.loops)
        mesh.vertAmount = len(meshEval.vertices)
        vertsPtr = meshEval.vertices[0].as_pointer()
        mesh.vertBuffer = ctypes.cast(vertsPtr, ctypes.POINTER(BlenderVert))
        loopsPtr = meshEval.loops[0].as_pointer()
        mesh.loopBuffer = ctypes.cast(loopsPtr, ctypes.POINTER(ctypes.c_int))
        facesPtr = meshEval.polygons[0].as_pointer()
        mesh.faceBuffer = ctypes.cast(facesPtr, ctypes.POINTER(ctypes.c_int))
        uvPtr = meshEval.uv_layers[0].data[0].as_pointer()
        mesh.uvBuffer = ctypes.cast(uvPtr, ctypes.POINTER(BlenderUv))

        workMesh = BlenderMeshData()

        uvgpLib.uvgpProjectOntoMesh(ctypes.pointer(mesh), ctypes.pointer(workMesh))

        nameUvgp = obj.name + ".Uvgp"
        print(nameUvgp)

        objUvgp = bpy.data.objects.get(nameUvgp, None)
        if not(objUvgp):
            print("Not objUvgp")
            meshUvgp = bpy.data.meshes.new(nameUvgp)
            objUvgp = bpy.data.objects.new(nameUvgp, meshUvgp)
            scene.collection.objects.link(objUvgp)
        else:
            print("Yes objUvgp")
            meshUvgpOld = objUvgp.data
            meshUvgpOld.name += ".Old"
            meshUvgp = bpy.data.meshes.new(nameUvgp)
            objUvgp.data = meshUvgp
            bpy.data.meshes.remove(meshUvgpOld)

        uvgpMesh = BlenderMeshData()
        print("workMesh.vertAmount ", workMesh.vertAmount)
        print("workMesh.loopAmount ", workMesh.loopAmount)
        print("workMesh.faceAmount ", workMesh.faceAmount)
        meshUvgp.vertices.add(workMesh.vertAmount)
        meshUvgp.loops.add(workMesh.loopAmount)
        meshUvgp.polygons.add(workMesh.faceAmount)
        uvgpMesh.vertAmount = len(meshUvgp.vertices)
        uvgpMesh.loopAmount = len(meshUvgp.loops)
        uvgpMesh.faceAmount = len(meshUvgp.polygons)
        vertsUvgpPtr = meshUvgp.vertices[0].as_pointer()
        uvgpMesh.vertBuffer = ctypes.cast(vertsUvgpPtr, ctypes.POINTER(BlenderVert))
        loopsUvgpPtr = meshUvgp.loops[0].as_pointer()
        uvgpMesh.loopBuffer = ctypes.cast(loopsUvgpPtr, ctypes.POINTER(ctypes.c_int))
        facesUvgpPtr = meshUvgp.polygons[0].as_pointer()
        uvgpMesh.faceBuffer = ctypes.cast(facesUvgpPtr, ctypes.POINTER(ctypes.c_int))

        uvgpLib.uvgpUpdateMesh(ctypes.pointer(uvgpMesh), ctypes.pointer(workMesh))

        meshUvgp.uv_layers.new(name="uvmap")
        uvPtr = meshUvgp.uv_layers[0].data[0].as_pointer()
        uvgpMesh.uvBuffer = ctypes.cast(uvPtr, ctypes.POINTER(BlenderUv))
        uvgpLib.uvgpUpdateMeshUv(ctypes.pointer(uvgpMesh), ctypes.pointer(workMesh))
        print("FinishedUpdating")
        objUvgp.data.update()

classes = [UVGP_OT_UvgpExportUvgpFile,
           UVGP_OT_UvgpUpdate,
           UVGP_OT_UvgpAssign,
           UVGP_OT_UvgpRemove,
           UVGP_OT_UvgpLoadUvgpFile]

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
