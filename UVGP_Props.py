import bpy

class UvgpTarget(bpy.types.PropertyGroup):
    id : bpy.props.IntProperty(default = -1)
    uvgpFilePath : bpy.props.StringProperty(name = "UVGP File")
    obj : bpy.props.PointerProperty(type = bpy.types.Object)

class UvgpProperties(bpy.types.PropertyGroup):
    nextTargetId : bpy.props.IntProperty(default = 0)

classes = [UvgpProperties,
           UvgpTarget]

#Register
def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.types.Object.uvgpTargetId = bpy.props.IntProperty(name = "UVGP Target ID", default = -1)
    bpy.types.Scene.uvgp = bpy.props.PointerProperty(type = UvgpProperties)
    bpy.types.Scene.uvgpTargets = bpy.props.CollectionProperty(name = "Targets", type = UvgpTarget)
    bpy.types.Scene.uvgpTargetsIndex = bpy.props.IntProperty(name = "Targets Index")

#Unregister
def unregister():
    for cls in classes:
        bpy.utils.unregister_class(cls)
    del bpy.types.Scene.Uvgp
