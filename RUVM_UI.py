import bpy

def RuvmExport(self, context):
    self.layout.operator("ruvm.ruvm_export_ruvm_file")

class RuvmParentPanel(bpy.types.Panel):
    bl_space_type = 'VIEW_3D'
    bl_region_type = "UI"
    bl_category = "RUVM"

class RUVM_UL_RuvmTargets(bpy.types.UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname):
        if self.layout_type in {'DEFAULT', 'COMPACT'}:
            row0 = layout.row(align = True)
            row0.prop(item, "obj", text = "", emboss = False, icon = 'MESH_CUBE')

class RUVM_PT_Ruvm(RuvmParentPanel, bpy.types.Panel):
    bl_idname = "RUVM_PT_Ruvm"
    bl_label = "RUVM"

    def draw(self, context):
        ruvm = context.scene.ruvm
        layout = self.layout
        row0 = layout.row()
        row0.template_list("RUVM_UL_RuvmTargets", "", context.scene, "ruvmTargets", context.scene, "ruvmTargetsIndex")
        row0.operator("ruvm.ruvm_assign", text = "Assign")
        row0.operator("ruvm.ruvm_remove", text = "Remove")
        row0.operator("ruvm.load_ruvm_file", text = "Load File")

classes = [RUVM_PT_Ruvm,
           RUVM_UL_RuvmTargets]

#Register
def register():
    print("Registering RUVM_UI")
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.types.TOPBAR_MT_file_export.append(RuvmExport)

#Unregister
def unregister():
    for cls in classes:
        bpy.utils.unregister_class(cls)
    bpy.types.TOPBAR_MT_file_export.remove(RuvmExport)
