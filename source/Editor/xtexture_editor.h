#ifndef XTEXTURE_EDITOR_H

#define XTEXTURE_EDITOR_H

#pragma once



// Standalone Texture editor - owned by the texture plugin, opened by hosts (E29 today) through

// the shared editor framework (source/Tools/Editor/). E10 is untouched; this editor reuses the

// preview mechanics E10 teaches (path rewrite, bitmap_inspector Load, material_mgr bitmap upload,

// 2D shaders/mesh/push-constants, draw_options) inside the NEW session/document/commands shape.

#include "source/Tools/Editor/xeditor_types.h"

#include "source/Tools/Editor/xeditor_registry.h"

#include "source/Tools/Editor/xeditor_dock_isolation.h"

#include "source/Tools/Editor/xeditor_inspector.h"

#include "source/Tools/Editor/xeditor_toolbar.h"

#include "source/Tools/Editor/xeditor_resource_tab.h"

#include "Plugins/xtexture.plugin/source/Editor/xtexture_editor_preview.h"

#include "Plugins/xtexture.plugin/source/xtexture_xgpu_rsc_loader.h"

#include "Plugins/xtexture.plugin/source/xtexture_rsc_descriptor.h"

#include "source/Examples/E10_TextureResourcePipeline/E10_AssetMgr.h"

#include "imgui.h"

#include <cstring>

#include <array>

#include <string>

#include <format>



namespace xtexture_editor

{

    //--------------------------------------------------------------------------------------------

    // Document

    //--------------------------------------------------------------------------------------------

    struct document : xeditor::IDocument

    {

        xresource::full_guid                                   m_Guid          = {};

        e10::library::guid                                     m_LibraryGuid   = {};

        std::wstring                                            m_DescriptorPath;

        std::unique_ptr<xresource_pipeline::descriptor::base>   m_pDescriptor;

        bool                                                     m_bDirty       = false;



        xresource::full_guid getGuid() const noexcept override { return m_Guid; }

        std::string getDisplayName() const noexcept override

        {

            std::string Name;

            e10::g_LibMgr.getNodeInfo(m_LibraryGuid, m_Guid, [&](e10::library_db::info_node& N){ Name = N.m_Info.m_Name; });

            return Name.empty() ? std::string("<texture>") : Name;

        }



        bool Load() noexcept override

        {

            e10::g_LibMgr.getNodeInfo(m_LibraryGuid, m_Guid, [&](e10::library_db::info_node& NodeInfo)

            {

                m_DescriptorPath = NodeInfo.m_Path;

                // Case-insensitive Info.txt -> Descriptor.txt (Cache uses Info.txt).

                for (size_t i = 0; i + 8 <= m_DescriptorPath.size(); ++i)

                {

                    auto Eq = true;

                    const wchar_t* From = L"info.txt";

                    for (size_t j = 0; j < 8; ++j)

                        if (towlower(m_DescriptorPath[i + j]) != From[j]) { Eq = false; break; }

                    if (Eq) { m_DescriptorPath.replace(i, 8, L"Descriptor.txt"); break; }

                }

            });

            if (m_DescriptorPath.empty()) return false;



            m_pDescriptor = xresource_pipeline::factory_base::Find(std::string_view{ "Texture" })->CreateDescriptor();

            xproperty::settings::context Context;

            if (auto Err = m_pDescriptor->Serialize(true, m_DescriptorPath, Context); Err)

                return false;

            m_bDirty = false;

            return true;

        }



        std::string Save() noexcept override

        {

            if (!m_pDescriptor) return "Texture editor: no descriptor loaded";

            xproperty::settings::context Context;

            if (auto Err = m_pDescriptor->Serialize(false, m_DescriptorPath, Context); Err)

                return std::string(Err.getMessage());

            e10::g_LibMgr.MakeDescriptorDirty({ m_LibraryGuid.m_Instance }, m_Guid);

            m_bDirty = false;

            return {};

        }



        bool isDirty() const noexcept override { return m_bDirty; }

    };



    //--------------------------------------------------------------------------------------------

    // Commands

    //--------------------------------------------------------------------------------------------

    struct set_srgb_cmd : xundo::command_base

    {

        document& m_Doc;

        set_srgb_cmd(xundo::system& System, document& Doc) noexcept : command_base(System, "SetSRGB", nullptr), m_Doc(Doc) { RegisterArguments(); }

        const char* getCommandHelp() const noexcept override { return "Sets the texture's sRGB flag (undoable). Usage: SetSRGB -Value 0|1"; }

        void RegisterArguments() noexcept override { m_hValue = m_Parser.addOption("Value", "0 or 1", true, 1); }

        std::string Redo() noexcept override

        {

            if (!m_Doc.m_pDescriptor) return "SetSRGB: no texture loaded";

            auto Arg = m_Parser.getOptionArgAs<std::string>(m_hValue, 0);

            if (std::holds_alternative<xerr>(Arg)) return "SetSRGB: bad arguments";

            static_cast<xtexture_rsc::descriptor*>(m_Doc.m_pDescriptor.get())->m_bSRGB = std::get<std::string>(Arg) != "0";

            m_Doc.m_bDirty = true;

            return {};

        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override

        {

            const bool Old = m_Doc.m_pDescriptor && static_cast<xtexture_rsc::descriptor*>(m_Doc.m_pDescriptor.get())->m_bSRGB;

            File.Write(Old);

        }

        void Undo(xundo::undo_file& File) noexcept override

        {

            bool Old = false; File.Read(Old);

            if (m_Doc.m_pDescriptor) static_cast<xtexture_rsc::descriptor*>(m_Doc.m_pDescriptor.get())->m_bSRGB = Old;

            m_Doc.m_bDirty = true;

        }

        xcmdline::parser::handle m_hValue;

    };



    struct set_generate_mips_cmd : xundo::command_base

    {

        document& m_Doc;

        set_generate_mips_cmd(xundo::system& System, document& Doc) noexcept : command_base(System, "SetGenerateMips", nullptr), m_Doc(Doc) { RegisterArguments(); }

        const char* getCommandHelp() const noexcept override { return "Sets whether mips are generated for this texture (undoable). Usage: SetGenerateMips -Value 0|1"; }

        void RegisterArguments() noexcept override { m_hValue = m_Parser.addOption("Value", "0 or 1", true, 1); }

        std::string Redo() noexcept override

        {

            if (!m_Doc.m_pDescriptor) return "SetGenerateMips: no texture loaded";

            auto Arg = m_Parser.getOptionArgAs<std::string>(m_hValue, 0);

            if (std::holds_alternative<xerr>(Arg)) return "SetGenerateMips: bad arguments";

            static_cast<xtexture_rsc::descriptor*>(m_Doc.m_pDescriptor.get())->m_bGenerateMips = std::get<std::string>(Arg) != "0";

            m_Doc.m_bDirty = true;

            return {};

        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override

        {

            const bool Old = m_Doc.m_pDescriptor && static_cast<xtexture_rsc::descriptor*>(m_Doc.m_pDescriptor.get())->m_bGenerateMips;

            File.Write(Old);

        }

        void Undo(xundo::undo_file& File) noexcept override

        {

            bool Old = false; File.Read(Old);

            if (m_Doc.m_pDescriptor) static_cast<xtexture_rsc::descriptor*>(m_Doc.m_pDescriptor.get())->m_bGenerateMips = Old;

            m_Doc.m_bDirty = true;

        }

        xcmdline::parser::handle m_hValue;

    };



    struct save_cmd : xundo::query_command_base

    {

        document& m_Doc;

        save_cmd(xundo::system& System, document& Doc) noexcept : query_command_base(System, "SaveTexture", nullptr), m_Doc(Doc) {}

        const char* getCommandHelp() const noexcept override { return "Saves the texture descriptor to disk. Usage: SaveTexture"; }

        void RegisterArguments() noexcept override {}

        std::string Query() noexcept override { auto Err = m_Doc.Save(); return Err.empty() ? "SaveTexture: saved" : Err; }

    };



    // Same shape as save_cmd: UI toolbar and headless TextureEditorCommand both go through here.

    // Persisting the descriptor is what queues compilation (E10 Compile button behavior).

    struct compile_cmd : xundo::query_command_base

    {

        document&                m_Doc;

        std::vector<std::string>& m_ValidationErrors;

        compile_cmd(xundo::system& System, document& Doc, std::vector<std::string>& ValidationErrors) noexcept

            : query_command_base(System, "CompileTexture", nullptr), m_Doc(Doc), m_ValidationErrors(ValidationErrors) {}

        const char* getCommandHelp() const noexcept override { return "Validates, saves the descriptor, and triggers compilation. Usage: CompileTexture"; }

        void RegisterArguments() noexcept override {}

        std::string Query() noexcept override

        {

            m_ValidationErrors.clear();

            if (!m_Doc.m_pDescriptor) return "CompileTexture: no texture loaded";

            m_Doc.m_pDescriptor->Validate(m_ValidationErrors);

            if (!m_ValidationErrors.empty())

                return std::format("CompileTexture: {} validation error(s)", m_ValidationErrors.size());

            auto Err = m_Doc.Save();

            return Err.empty() ? "CompileTexture: saved (compile queued)" : Err;

        }

    };



    struct undo_cmd : xundo::query_command_base

    {

        undo_cmd(xundo::system& System) noexcept : query_command_base(System, "UndoTexture", nullptr) {}

        const char* getCommandHelp() const noexcept override { return "Undoes the last texture command. Usage: UndoTexture"; }

        void RegisterArguments() noexcept override {}

        std::string Query() noexcept override { m_System.Undo(); return "UndoTexture: done"; }

    };



    struct redo_cmd : xundo::query_command_base

    {

        redo_cmd(xundo::system& System) noexcept : query_command_base(System, "RedoTexture", nullptr) {}

        const char* getCommandHelp() const noexcept override { return "Redoes the last undone texture command. Usage: RedoTexture"; }

        void RegisterArguments() noexcept override {}

        std::string Query() noexcept override { m_System.Redo(); return "RedoTexture: done"; }

    };



    //--------------------------------------------------------------------------------------------

    // Session

    //--------------------------------------------------------------------------------------------

    struct session

    {

        document                m_Document;

        xundo::system            m_Undo;

        set_srgb_cmd             m_SetSRGB;

        set_generate_mips_cmd    m_SetGenerateMips;

        save_cmd                 m_Save;

        std::vector<std::string> m_ValidationErrors;

        compile_cmd              m_Compile;

        undo_cmd                 m_UndoCmd;

        redo_cmd                 m_RedoCmd;

        bool                     m_bOpen = true;

        bool                     m_bRequestFocus = false;

        preview::runtime         m_Preview{};

        xeditor::inspector_panel m_DescriptorInspector{"Texture Descriptor"};

        xeditor::inspector_panel m_ViewerInspector{"Texture Viewer"};

        bool                     m_bInspectorsBound = false;

        std::string              m_ViewerWindowTitle;

        std::string              m_DescriptorWindowTitle;

        std::string              m_PreviewWindowTitle;

        std::shared_ptr<e10::compilation::historical_entry::log> m_CompilationLog =

            std::make_shared<e10::compilation::historical_entry::log>(

                e10::compilation::historical_entry::communication{

                    .m_Result = e10::compilation::historical_entry::result::SUCCESS });

        bool                     m_bReloadPreview = false;

        bool                     m_bCompilationCallbackRegistered = false;



        session(xresource::full_guid Guid, e10::library::guid LibraryGuid, xgpu::device* pDevice = nullptr) noexcept

            : m_SetSRGB(m_Undo, m_Document), m_SetGenerateMips(m_Undo, m_Document)

            , m_Save(m_Undo, m_Document), m_Compile(m_Undo, m_Document, m_ValidationErrors), m_UndoCmd(m_Undo), m_RedoCmd(m_Undo)

        {

            m_Document.m_Guid        = Guid;

            m_Document.m_LibraryGuid = LibraryGuid;

            if (auto Err = m_Undo.Init({}, false); !Err.empty()) printf("Texture editor session Init: %s\n", Err.c_str());

            m_Document.Load();



            char IdSuffix[40];

            snprintf(IdSuffix, sizeof(IdSuffix), "##%016llX%016llX", (unsigned long long)Guid.m_Instance.m_Value, (unsigned long long)Guid.m_Type.m_Value);

            m_ViewerWindowTitle     = std::string("Rendering Options") + IdSuffix;

            m_DescriptorWindowTitle = std::string("Description") + IdSuffix;

            m_PreviewWindowTitle    = std::string("Preview") + IdSuffix;



            if (pDevice)

            {

                m_Preview.Init(*pDevice);

                if (!m_Document.m_DescriptorPath.empty())

                    m_Preview.LoadFromDescriptorPath(m_Document.m_DescriptorPath);

            }



            BindInspectors();

            RegisterCompilationCallback();

        }



        ~session() noexcept

        {

            if (m_bCompilationCallbackRegistered)

                e10::g_LibMgr.m_OnCompilationState.RemoveDelegates(this);

        }



        void RegisterCompilationCallback() noexcept

        {

            if (m_bCompilationCallbackRegistered) return;

            e10::g_LibMgr.m_OnCompilationState.Register<&session::OnCompilationState>(*this);

            m_bCompilationCallbackRegistered = true;

        }



        void OnCompilationState(e10::library_mgr&, e10::library::guid, xresource::full_guid CompilingEntry,

                                std::shared_ptr<e10::compilation::historical_entry::log>& LogInformation) noexcept

        {

            if (CompilingEntry != m_Document.m_Guid) return;

            if (m_CompilationLog.get() != LogInformation.get())

                m_CompilationLog = LogInformation;

            if (!m_CompilationLog) return;



            e10::compilation::historical_entry::result Results;

            {

                xcontainer::lock::scope lk(*m_CompilationLog);

                Results = m_CompilationLog->get().m_Result;

            }

            if (Results == e10::compilation::historical_entry::result::SUCCESS

                || Results == e10::compilation::historical_entry::result::SUCCESS_WARNINGS)

            {

                m_bReloadPreview = true;

            }

        }



        static void ToolbarSave(void* pUser) noexcept

        {

            auto* Self = static_cast<session*>(pUser);

            // Same path as headless TextureEditorCommand -Cmd SaveTexture (not a second Save()).

            auto _r = Self->m_Undo.Query("SaveTexture"); (void)_r;

        }



        static void ToolbarCompile(void* pUser) noexcept

        {

            auto* Self = static_cast<session*>(pUser);

            // Same path as headless TextureEditorCommand -Cmd CompileTexture.

            auto _r = Self->m_Undo.Query("CompileTexture"); (void)_r;

        }



        void TickCompilationFeedback() noexcept

        {

            if (m_Document.m_pDescriptor)

            {

                m_ValidationErrors.clear();

                m_Document.m_pDescriptor->Validate(m_ValidationErrors);

            }

            if (m_bReloadPreview && m_Preview.m_pDevice)

            {

                m_bReloadPreview = false;

                if (!m_Document.m_DescriptorPath.empty())

                    m_Preview.LoadFromDescriptorPath(m_Document.m_DescriptorPath);

            }

        }



        void RenderToolbar() noexcept

        {

            xeditor::toolbar_model Bar{};

            Bar.m_pUndo             = &m_Undo;

            Bar.m_bDirty            = m_Document.isDirty();

            Bar.m_bCanCompile       = m_Document.m_pDescriptor != nullptr;

            Bar.m_Log               = m_CompilationLog;

            Bar.m_pValidationErrors = &m_ValidationErrors;

            Bar.m_OnSave            = &session::ToolbarSave;

            Bar.m_OnCompile         = &session::ToolbarCompile;

            Bar.m_pUser             = this;

            xeditor::RenderEditorToolbar(Bar);

        }



        

        void OnDescriptorChange(xproperty::inspector&, const xproperty::ui::undo::cmd& Cmd) noexcept

        {

            if (!m_Document.m_pDescriptor) return;

            m_Document.m_bDirty = true;



            const auto& Name = Cmd.m_Name;

            const bool bSRGB = Name.find("sRGB") != std::string::npos || Name.find("SRGB") != std::string::npos;

            const bool bMips = Name.find("GenerateMips") != std::string::npos;

            if (!bSRGB && !bMips) return;



            std::array<char, 256> NewBuf{};

            if (Cmd.m_NewValue.m_pType)

                xproperty::settings::AnyToString(NewBuf, Cmd.m_NewValue);

            // Bool any usually stringifies as "true"/"false" or "1"/"0"

            const bool bNew = (NewBuf[0] == '1' || NewBuf[0] == 't' || NewBuf[0] == 'T');



            if (Cmd.m_Original.m_pType && Cmd.m_pClassObject && Cmd.m_pPropObject)

            {

                xproperty::sprop::container::prop Prop{ Cmd.m_Name, Cmd.m_Original };

                std::string Error;

                xproperty::sprop::setProperty(Error, Cmd.m_pClassObject, *Cmd.m_pPropObject, Prop, m_DescriptorInspector.m_Context);

            }



            char Buf[64];

            if (bSRGB)

            {

                snprintf(Buf, sizeof(Buf), "SetSRGB -Value %d", bNew ? 1 : 0);

                auto _r = m_Undo.Execute(Buf); (void)_r;

            }

            else

            {

                snprintf(Buf, sizeof(Buf), "SetGenerateMips -Value %d", bNew ? 1 : 0);

                auto _r = m_Undo.Execute(Buf); (void)_r;

            }

        }



        void BindInspectors() noexcept

        {

            m_bInspectorsBound = false;

            if (!m_Document.m_pDescriptor) return;



            m_DescriptorInspector.Clear();

            m_DescriptorInspector.m_Inspector.AppendEntity();

            m_DescriptorInspector.AppendComponent(*m_Document.m_pDescriptor->getProperties(), m_Document.m_pDescriptor.get());



            m_DescriptorInspector.m_Inspector.m_OnChangeEvent.m_Delegates.clear();

            m_DescriptorInspector.m_Inspector.m_OnChangeEvent.Register<&session::OnDescriptorChange>(*this);



            m_ViewerInspector.Clear();

            m_ViewerInspector.m_Inspector.AppendEntity();

            m_ViewerInspector.AppendComponent(*xproperty::getObject(m_Preview.m_DrawControls), &m_Preview.m_DrawControls);

            m_ViewerInspector.AppendComponent(*xproperty::getObject(m_Preview.m_DrawOptions), &m_Preview.m_DrawOptions);

            m_ViewerInspector.AppendComponent(*xproperty::getObject(m_Preview.m_BitmapInspector), &m_Preview.m_BitmapInspector);



            m_bInspectorsBound = true;

        }





        void EnsureDevice(xgpu::device* pDevice) noexcept

        {

            if (!pDevice) return;

            if (!m_Preview.m_bGpuReady)

            {

                m_Preview.Init(*pDevice);

                if (!m_Document.m_DescriptorPath.empty())

                    m_Preview.LoadFromDescriptorPath(m_Document.m_DescriptorPath);

            }

        }



        void Focus() noexcept

        {

            m_bOpen = true;

            m_bRequestFocus = true;

        }



        void Render() noexcept

        {

            // Root tab: type icon + asset name (not "Texture Editor"); taller than default tabs.

            const std::string DisplayName = xeditor::ResolveResourceDisplayName(

                m_Document.m_LibraryGuid, m_Document.m_Guid, "Texture");

            char StableId[40];

            std::snprintf(StableId, sizeof(StableId), "%016llX%016llX"

                , (unsigned long long)m_Document.m_Guid.m_Instance.m_Value

                , (unsigned long long)m_Document.m_Guid.m_Type.m_Value);

            char Title[256];

            xeditor::FormatEditorRootTabTitle(Title, sizeof(Title), DisplayName.c_str(), StableId);



            ImGui::SetNextWindowSize(ImVec2(520, 640), ImGuiCond_FirstUseEver);

            // Same as Level Editor (E29_EditorTabs.h RenderParentEditorDockspace): zero window

            // padding so the MenuBar strip sits flush on the dockspace with no hairline gap.

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

            // Unclassed peer of Level Editor (see prior docking notes). Always-tab-bar helps

            // tab-switching when several top-level editors share a dock node.

            ImGuiWindowFlags Flags = ImGuiWindowFlags_None;

            Flags |= ImGuiWindowFlags_MenuBar;

#ifdef ImGuiWindowFlags_DockingAlwaysTabBar

            Flags |= ImGuiWindowFlags_DockingAlwaysTabBar;

#endif

            if (m_bRequestFocus)

            {

                ImGui::SetNextWindowFocus();

                m_bRequestFocus = false;

            }



            // Title is "Name###guid" — stable id; normal theme tab/menu sizes.

            const bool bVisible = ImGui::Begin(Title, &m_bOpen, Flags);

            xeditor::DrawEditorRootTabIcon(m_Preview.m_pDevice, m_Document.m_Guid.m_Type); // every frame

            if (bVisible)

            {

                TickCompilationFeedback();

                RenderToolbar();



                if (m_Document.m_pDescriptor && m_bInspectorsBound)

                {

                    // The top-level window hosts the framework toolbar, then a dockspace - no

                    // inline content of its own - exactly mirroring how E29's OWN "Level Editor"

                    // root window works (its real content is entirely sub-panels; the root just

                    // hosts the dockspace). Mixing "some inline content" with "a nested dockspace

                    // for only some of the panels" (an earlier version of this function) produced

                    // a visibly broken layout (panels squeezed into a tiny leftover corner) - one

                    // dockspace spanning the FULL window, with Preview/Rendering

                    // Options/Description as three equal, real dockable panels, is the fix.

                    //

                    // Per-instance ImGuiWindowClass, keyed by this resource's own guid

                    // (xeditor::DockClassForResource) - isolation belongs here, one level down

                    // from the top-level window (which stays unclassed so it can coexist with

                    // "Level Editor" as a normal peer) - direct user correction: "these

                    // inspectors belong only to the texture editor."

                    const auto WindowClass = xeditor::DockClassForResource(m_Document.m_Guid);

                    const ImGuiID DockId = ImGui::GetID("TextureEditorDock");

                    if (ImGui::DockBuilderGetNode(DockId) == nullptr)

                    {

                        ImGui::DockBuilderAddNode(DockId, ImGuiDockNodeFlags_DockSpace);

                        ImGui::DockBuilderSetNodeSize(DockId, ImGui::GetContentRegionAvail());

                        ImGuiID LeftId = 0, RightId = 0, CenterId = 0;

                        ImGui::DockBuilderSplitNode(DockId, ImGuiDir_Left, 0.25f, &LeftId, &CenterId);

                        ImGui::DockBuilderSplitNode(CenterId, ImGuiDir_Right, 0.35f, &RightId, &CenterId);

                        ImGui::DockBuilderDockWindow(m_ViewerWindowTitle.c_str(), LeftId);

                        ImGui::DockBuilderDockWindow(m_PreviewWindowTitle.c_str(), CenterId);

                        ImGui::DockBuilderDockWindow(m_DescriptorWindowTitle.c_str(), RightId);

                        ImGui::DockBuilderFinish(DockId);

                    }

                    ImGui::DockSpace(DockId, ImGui::GetContentRegionAvail(), ImGuiDockNodeFlags_None, &WindowClass);
                    xeditor::FinishFullEditorDockspace(DockId, m_Document.m_Guid);



                    ImGui::SetNextWindowClass(&WindowClass);

                    if (ImGui::Begin(m_PreviewWindowTitle.c_str()))

                    {

                        const ImVec2 Avail = ImGui::GetContentRegionAvail();

                        // Claim the canvas like E10's full-window input target (docked panel equivalent).

                        ImGui::InvisibleButton("##TexturePreviewCanvas", Avail);

                        m_Preview.HandleInput(Avail.x, Avail.y);



                        if (m_Preview.m_bGpuReady)

                        {

                            // AddCustomRenderCallback passes GetWindowSize(); E10 uses one viewport size for

                            // both input and draw — keep Avail (canvas) for both.

                            xgpu::tools::imgui::AddCustomRenderCallback([this, Avail](xgpu::cmd_buffer& CmdBuffer, const ImVec2&, const ImVec2&)

                            {

                                if (!m_bOpen) return;

                                m_Preview.Draw(CmdBuffer, Avail.x, Avail.y);

                            });

                        }

                        else

                        {

                            ImGui::TextDisabled("Preview needs a GPU device (open from E29).");

                        }

                        if (!m_Preview.m_bHasTexture)

                            ImGui::TextUnformatted("No compiled resource yet (compile the texture, then reopen).");

                    }

                    ImGui::End();



                    ImGui::SetNextWindowClass(&WindowClass);

                    if (ImGui::Begin(m_ViewerWindowTitle.c_str()))

                        m_ViewerInspector.Show();

                    ImGui::End();



                    ImGui::SetNextWindowClass(&WindowClass);

                    if (ImGui::Begin(m_DescriptorWindowTitle.c_str()))

                        m_DescriptorInspector.Show();

                    ImGui::End();

                }

                else if (!m_Document.m_pDescriptor)

                {

                    ImGui::TextUnformatted("Failed to load texture descriptor.");

                }

            }

            ImGui::End();

            ImGui::PopStyleVar();

        }



    };



        inline xeditor::editor_descriptor MakeEditorDescriptor() noexcept
    {
        xeditor::editor_descriptor Descriptor;
        Descriptor.m_TypeGuid          = xrsc::texture_type_guid_v;
        Descriptor.m_TypeName          = "Texture";
        Descriptor.m_bSupportsHeadless = true;
        Descriptor.m_CreateDocument    = [](xresource::full_guid Guid) -> std::unique_ptr<xeditor::IDocument>
        {
            auto Doc = std::make_unique<document>();
            Doc->m_Guid = Guid;
            return Doc;
        };
        return Descriptor;
    }



    inline const xeditor::auto_register g_Registration{ MakeEditorDescriptor() };

}



#endif // XTEXTURE_EDITOR_H

