import bpy

class RuvmTarget(bpy.types.PropertyGroup):
    id : bpy.props.IntProperty(default = -1)
    ruvmFilePath : bpy.props.StringProperty(name = "RUVM File")
    obj : bpy.props.PointerProperty(type = bpy.types.Object)

class RuvmProperties(bpy.types.PropertyGroup):
    nextTargetId : bpy.props.IntProperty(default = 0)

classes = [RuvmProperties,
           RuvmTarget]

#Register
def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.types.Object.ruvmTargetId = bpy.props.IntProperty(name = "RUVM Target ID", default = -1)
    bpy.types.Scene.ruvm = bpy.props.PointerProperty(type = RuvmProperties)
    bpy.types.Scene.ruvmTargets = bpy.props.CollectionProperty(name = "Targets", type = RuvmTarget)
    bpy.types.Scene.ruvmTargetsIndex = bpy.props.IntProperty(name = "Targets Index")

#Unregister
def unregister():
    for cls in classes:
        bpy.utils.unregister_class(cls)
    del bpy.types.Scene.Ruvm
