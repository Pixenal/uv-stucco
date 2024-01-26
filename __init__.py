bl_info = {
    "name": "Reverse UV Mapper",
    "description": "An addon for projecting geometry onto the surface of a mesh, using UVs",
    "author": "Pixenal",
    "version": (1, 0),
    "blender": (3, 6, 0),
    "location": "View",
    "category": "3D View"
}

import importlib

if ("bpy" in locals()):
    importlib.reload(RUVM_Props)
    importlib.reload(RUVM_Ops)
    importlib.reload(RUVM_UI)
else:
    from . import RUVM_Props
    from . import RUVM_Ops
    from . import RUVM_UI

import bpy

#Register
def register():
    print("Registering RUVM")
    RUVM_Props.register()
    RUVM_Ops.register()
    RUVM_UI.register()

#Unregister
def unregister():
    print("Registering RUVM")
    RUVM_Props.unregister()
    RUVM_Ops.unregister()
    RUVM_UI.unregister()

