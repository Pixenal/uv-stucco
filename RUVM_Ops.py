import bpy
import ctypes
import numpy
import bmesh
from bpy.app.handlers import persistent

#ruvmLib = ctypes.cdll.LoadLibrary("T:/workshop_folders/RUVM/Win64/RUVM.dll")
ruvmLib = ctypes.cdll.LoadLibrary("/run/media/calebdawson/Tuna/workshop_folders/RUVM/Linux/RUVM.so")
#ruvmLib = ctypes.cdll.LoadLibrary("T:\workshop_folders/RUVMWin/RUVM/x64/Debug/RUVM.dll")

class Vec2(ctypes.Structure):
    _fields_ = [("x", ctypes.c_float),
                ("y", ctypes.c_float)]

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

class Vec3(ctypes.Structure):
    _fields_ = [("x", ctypes.c_float),
                ("y", ctypes.c_float),
                ("z", ctypes.c_float)]

class BlenderMeshData(ctypes.Structure):
    _fields_ = [("vertAmount", ctypes.c_int),
                ("vertBuffer", ctypes.POINTER(Vec3)),
                ("loopAmount", ctypes.c_int),
                ("loopBuffer", ctypes.POINTER(ctypes.c_int)),
                ("faceAmount", ctypes.c_int),
                ("faceBuffer", ctypes.POINTER(ctypes.c_int)),
                ("uvBuffer", ctypes.POINTER(Vec2))]

class RUVM_OT_RuvmExportRuvmFile(bpy.types.Operator):
    bl_idname = "ruvm.ruvm_export_ruvm_file"
    bl_label = "RUVM Export"
    bl_options = {'REGISTER'}

    def execute(self, context):
        if (len(context.selected_objects) == 0):
            print("RUVM export failed, no objects selected.")
            return {'CANCELLED'}
        if (len(context.selected_objects) > 1):
            print("RUVM export failed, more than one object selected.")
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
        vertsPtrFloat = ctypes.cast(vertsPtr, ctypes.POINTER(Vec3))
        loopsPtr = meshEval.loops[0].as_pointer()
        loopsPtrInt = ctypes.cast(loopsPtr, ctypes.POINTER(ctypes.c_int))
        facesPtr = meshEval.polygons[0].as_pointer()
        normals = numpy.zeros(loopAmount * 3, dtype = numpy.float32)
        meshEval.loops.foreach_get("normal", normals)
        facesPtrInt = ctypes.cast(facesPtr, ctypes.POINTER(ctypes.c_int))

        ruvmLib.RuvmExportRuvmFile.argtypes = (ctypes.c_int, ctypes.POINTER(Vec3),
                                               ctypes.c_int, ctypes.POINTER(ctypes.c_int),
                                               numpy.ctypeslib.ndpointer(ctypes.c_float, flags="C_CONTIGUOUS"),
                                               ctypes.c_int, ctypes.POINTER(ctypes.c_int))
        ruvmLib.RuvmExportRuvmFile(vertAmount, vertsPtrFloat, loopAmount, loopsPtrInt, normals, faceAmount, facesPtrInt)

        return {'FINISHED'}

class RUVM_OT_RuvmUpdate(bpy.types.Operator):
    bl_idname = "ruvm.ruvm_update"
    bl_label = "RUVM Update"

    def __init__(self):
        print("Initializing RUVM")

    def __del__(self):
        print("Ending RUVM")

    def execute(self, context):
        return {'FINISHED'}

    def modal(self, context, event):
        print("Updating RUVM")
        return {'RUNNING_MODAL'}

    def invoke(self, context, event):
        return {'RUNNING_MODAL'}

class RUVM_OT_RuvmAssign(bpy.types.Operator):
    bl_idname = "ruvm.ruvm_assign"
    bl_label = "RUVM Assign"
    bl_options = {'REGISTER'}

    def execute(self, context):
        ruvm = context.scene.ruvm
        if len(context.selected_objects) == 0:
            return {'CANCELLED'}
        for obj in context.selected_objects:
            exists = False
            for target in context.scene.ruvmTargets:
                if target.obj == obj:
                    exists = True
                    break
            if exists:
                continue
            newTarget = context.scene.ruvmTargets.add()
            newTarget.obj = obj.id_data
            newTarget.id = ruvm.nextTargetId
            obj.ruvmTargetId = ruvm.nextTargetId
            ruvm.nextTargetId += 1
        return {'FINISHED'}

class RUVM_OT_RuvmLoadRuvmFile(bpy.types.Operator):
    bl_idname = "ruvm.load_ruvm_file"
    bl_label = "Load RUVM File"
    bl_options = {"REGISTER"}

    def execute(self, context):
        filePath = "/run/media/calebdawson/Tuna/workshop_folders/RUVM/TestOutputDir/File.ruvm"
        filePathUtf8 = filePath.encode('utf-8')
        ruvmLib.ruvmLoadRuvmFile(filePathUtf8)
        return {'FINISHED'}

class RUVM_OT_RuvmRemove(bpy.types.Operator):
    bl_idname = "ruvm.ruvm_remove"
    bl_label = "RUVM Remove"
    bl_options = {"REGISTER"}

    def execute(self, context):
        scene = context.scene
        if scene.ruvmTargetsIndex >= len(scene.ruvmTargets):
            return {'CANCELLED'}
        del scene.ruvmTargets[scene.ruvmTargetsIndex].obj["ruvmTargetId"]
        scene.ruvmTargets.remove(scene.ruvmTargetsIndex)
        return {'FINISHED'}

@persistent
def ruvmDepsgraphUpdatePostHandler(dummy):
    scene = bpy.context.scene
    depsgraph = bpy.context.evaluated_depsgraph_get()
    for target in scene.ruvmTargets:
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
        mesh.vertBuffer = ctypes.cast(vertsPtr, ctypes.POINTER(Vec3))
        loopsPtr = meshEval.loops[0].as_pointer()
        mesh.loopBuffer = ctypes.cast(loopsPtr, ctypes.POINTER(ctypes.c_int))
        facesPtr = meshEval.polygons[0].as_pointer()
        mesh.faceBuffer = ctypes.cast(facesPtr, ctypes.POINTER(ctypes.c_int))
        uvPtr = meshEval.uv_layers[0].data[0].as_pointer()
        mesh.uvBuffer = ctypes.cast(uvPtr, ctypes.POINTER(Vec2))

        workMesh = BlenderMeshData()

        ruvmLib.ruvmMapToMesh(ctypes.pointer(mesh), ctypes.pointer(workMesh))

        nameRuvm = obj.name + ".Ruvm"
        print(nameRuvm)

        objRuvm = bpy.data.objects.get(nameRuvm, None)
        if not(objRuvm):
            print("Not objRuvm")
            meshRuvm = bpy.data.meshes.new(nameRuvm)
            objRuvm = bpy.data.objects.new(nameRuvm, meshRuvm)
            scene.collection.objects.link(objRuvm)
        else:
            print("Yes objRuvm")
            meshRuvmOld = objRuvm.data
            meshRuvmOld.name += ".Old"
            meshRuvm = bpy.data.meshes.new(nameRuvm)
            objRuvm.data = meshRuvm
            bpy.data.meshes.remove(meshRuvmOld)

        ruvmMesh = BlenderMeshData()
        print("workMesh.vertAmount ", workMesh.vertAmount)
        print("workMesh.loopAmount ", workMesh.loopAmount)
        print("workMesh.faceAmount ", workMesh.faceAmount)
        meshRuvm.vertices.add(workMesh.vertAmount)
        meshRuvm.loops.add(workMesh.loopAmount)
        meshRuvm.polygons.add(workMesh.faceAmount)
        ruvmMesh.vertAmount = len(meshRuvm.vertices)
        ruvmMesh.loopAmount = len(meshRuvm.loops)
        ruvmMesh.faceAmount = len(meshRuvm.polygons)
        vertsRuvmPtr = meshRuvm.vertices[0].as_pointer()
        ruvmMesh.vertBuffer = ctypes.cast(vertsRuvmPtr, ctypes.POINTER(Vec3))
        loopsRuvmPtr = meshRuvm.loops[0].as_pointer()
        ruvmMesh.loopBuffer = ctypes.cast(loopsRuvmPtr, ctypes.POINTER(ctypes.c_int))
        facesRuvmPtr = meshRuvm.polygons[0].as_pointer()
        ruvmMesh.faceBuffer = ctypes.cast(facesRuvmPtr, ctypes.POINTER(ctypes.c_int))

        ruvmLib.ruvmUpdateMesh(ctypes.pointer(ruvmMesh), ctypes.pointer(workMesh))

        meshRuvm.uv_layers.new(name="uvmap")
        uvPtr = meshRuvm.uv_layers[0].data[0].as_pointer()
        ruvmMesh.uvBuffer = ctypes.cast(uvPtr, ctypes.POINTER(Vec2))
        ruvmLib.ruvmUpdateMeshUv(ctypes.pointer(ruvmMesh), ctypes.pointer(workMesh))
        print("FinishedUpdating")
        objRuvm.data.update()

classes = [RUVM_OT_RuvmExportRuvmFile,
           RUVM_OT_RuvmUpdate,
           RUVM_OT_RuvmAssign,
           RUVM_OT_RuvmRemove,
           RUVM_OT_RuvmLoadRuvmFile]

#Register
def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.app.handlers.depsgraph_update_post.append(ruvmDepsgraphUpdatePostHandler)

#Unregister
def unregister():
    for cls in classes:
        bpy.utils.unregister_class(cls)
    bpy.app.handlers.depsgraph_update_post.remove(ruvmDepsgraphUpdatePostHandler)
