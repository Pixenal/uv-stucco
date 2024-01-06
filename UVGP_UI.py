import bpy

def UvgpExport(self, context):
    self.layout.operator("uvgp.uvgp_export_uvgp_file")

class UvgpParentPanel(bpy.types.Panel):
    bl_space_type = 'VIEW_3D'
    bl_region_type = "UI"
    bl_category = "UVGP"

class UVGP_UL_UvgpTargets(bpy.types.UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname):
        if self.layout_type in {'DEFAULT', 'COMPACT'}:
            row0 = layout.row(align = True)
            row0.prop(item, "obj", text = "", emboss = False, icon = 'MESH_CUBE')

class UVGP_PT_Uvgp(UvgpParentPanel, bpy.types.Panel):
    bl_idname = "UVGP_PT_Uvgp"
    bl_label = "UVGP"

    def draw(self, context):
        uvgp = context.scene.uvgp
        layout = self.layout
        row0 = layout.row()
        row0.template_list("UVGP_UL_UvgpTargets", "", context.scene, "uvgpTargets", context.scene, "uvgpTargetsIndex")
        row0.operator("uvgp.uvgp_assign", text = "Assign")
        row0.operator("uvgp.uvgp_remove", text = "Remove")
        row0.operator("uvgp.load_uvgp_file", text = "Load File")

classes = [UVGP_PT_Uvgp,
           UVGP_UL_UvgpTargets]

#Register
def register():
    print("Registering UVGP_UI")
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.types.TOPBAR_MT_file_export.append(UvgpExport)

#Unregister
def unregister():
    for cls in classes:
        bpy.utils.unregister_class(cls)
    bpy.types.TOPBAR_MT_file_export.remove(UvgpExport)
