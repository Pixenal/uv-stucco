bl_info = {
    "name": "UVGP",
    "description": "An addon for projecting geometry onto the surface of a mesh, using UVs",
    "author": "Pixenal",
    "version": (1, 0),
    "blender": (3, 6, 0),
    "location": "View",
    "category": "3D View"
}

import importlib

if ("bpy" in locals()):
    importlib.reload(UVGP_Props)
    importlib.reload(UVGP_Ops)
    importlib.reload(UVGP_UI)
else:
    from . import UVGP_Props
    from . import UVGP_Ops
    from . import UVGP_UI

import bpy

#Register
def register():
    print("Registering UVGP")
    UVGP_Props.register()
    UVGP_Ops.register()
    UVGP_UI.register()

#Unregister
def unregister():
    print("Registering UVGP")
    UVGP_Props.unregister()
    UVGP_Ops.unregister()
    UVGP_UI.unregister()

