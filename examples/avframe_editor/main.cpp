#include "frame_editor.hpp"

#include <cstdio>
#include <string>

#include <SDL.h>
#include <SDL_opengl.h>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

namespace {

bool InitSdlAndGl(SDL_Window** window, SDL_GLContext* gl_context) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    *window = SDL_CreateWindow(
        "AVFrame Editor (Framework)",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1366,
        768,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (!*window) {
        return false;
    }

    *gl_context = SDL_GL_CreateContext(*window);
    if (!*gl_context) {
        return false;
    }

    SDL_GL_MakeCurrent(*window, *gl_context);
    SDL_GL_SetSwapInterval(1);
    return true;
}

void DrawEditorUI(FrameEditor& editor, char* input_path, size_t input_size, char* output_path, size_t output_size, std::string& status) {
    ImGui::Begin("AVFrame Timestamp Editor");

    ImGui::InputText("Input file", input_path, input_size);
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        std::string err;
        if (editor.Load(input_path, err)) {
            status = "Loaded packets: " + std::to_string(editor.EditCount());
        } else {
            status = "Load failed: " + err;
        }
    }

    ImGui::InputText("Output file", output_path, output_size);
    ImGui::SameLine();
    if (ImGui::Button("Save As")) {
        std::string err;
        if (editor.SaveAs(output_path, err)) {
            status = "Save succeeded";
        } else {
            status = "Save failed: " + err;
        }
    }

    ImGui::Separator();
    ImGui::TextWrapped("Status: %s", status.c_str());
    
    ImGui::SetNextItemWidth(150);
    static int stream_filter_idx = 0;
    ImGui::Combo("Stream Filter", &stream_filter_idx, [](void* data, int idx) {
        if (idx == 0) {
            return "All";
        }
        int stream_index = idx - 1;
        static char buff[32];
        auto stream = static_cast<FrameEditor*>(data)->getStream(stream_index);
        sprintf(buff, "%s %d", av_get_media_type_string((AVMediaType)stream->type), stream_index);
        return (const char*)buff;
    }, &editor, static_cast<int>(editor.StreamCount() + 1));
    ImGui::SameLine();
    static bool pts_dts = false;
    ImGui::Checkbox("PTS=DTS", &pts_dts);
    ImGui::SameLine();
    static bool drop_util_flag = true;
    ImGui::Checkbox("DropToFlag", &drop_util_flag);

    if (ImGui::BeginTable("packets", 10, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
        ImGui::TableSetupColumn("Idx");
        ImGui::TableSetupColumn("Stream");
        ImGui::TableSetupColumn("PTS");
        ImGui::TableSetupColumn("DTS");
        ImGui::TableSetupColumn("Orig PTS");
        ImGui::TableSetupColumn("Orig DTS");
        ImGui::TableSetupColumn("Size");
        ImGui::TableSetupColumn("Pos");
        ImGui::TableSetupColumn("Flags");
        ImGui::TableSetupColumn("Delete");
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        auto stm = editor.getStream(stream_filter_idx - 1);
        clipper.Begin(stm ? stm->size() : editor.EditCount());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                PacketEdit* e = nullptr;
                if (stm) {
                    e = editor.Edit(stm->index[row]);
                } else {
                    e = editor.Edit(row);
                }
                if(!e) {
                    continue;
                }
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%lld", static_cast<long long>(e->packet_index));

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", e->stream_index);

                ImGui::TableSetColumnIndex(2);
                ImGui::PushID(static_cast<int>(e->packet_index * 10 + 1));
                if (ImGui::InputScalar("##pts", ImGuiDataType_S64, &e->edited_pts) && pts_dts) {
                    e->edited_dts = e->edited_pts;
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(3);
                ImGui::PushID(static_cast<int>(e->packet_index * 10 + 2));
                if (ImGui::InputScalar("##dts", ImGuiDataType_S64, &e->edited_dts) && pts_dts) {
                    e->edited_pts = e->edited_dts;
                }
                ImGui::PopID();

                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%lld", static_cast<long long>(e->original_pts));

                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%lld", static_cast<long long>(e->original_dts));

                ImGui::TableSetColumnIndex(6);
                ImGui::Text("%d", e->size);

                ImGui::TableSetColumnIndex(7);
                ImGui::Text("%lld", static_cast<long long>(e->pos));

                ImGui::TableSetColumnIndex(8);
                ImGui::Text("%d", e->flags);

                ImGui::TableSetColumnIndex(9);
                ImGui::PushID(static_cast<int>(e->packet_index * 10 + 3));
                if (ImGui::Checkbox("##delete", &e->deleted) && drop_util_flag) {
                    if (stm) {
                        for(int j = row + 1; j < stm->size(); ++j) {
                            auto n = editor.Edit(stm->index[j]);
                            if (!n || n->flags) {
                                break;
                            }
                            n->deleted = e->deleted;
                        }
                    } else {
                        for (int j = row + 1; j<editor.EditCount(); ++j) {
                            auto n = editor.Edit(j);
                            if (!n) break;
                            if (n->stream_index != e->stream_index) continue;
                            if (n->flags) break;   
                            n->deleted = e->deleted;
                        }
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace

int main(int, char**) {
    SDL_Window* window = nullptr;
    SDL_GLContext gl_context = nullptr;

    if (!InitSdlAndGl(&window, &gl_context)) {
        std::fprintf(stderr, "Failed to initialize SDL2/OpenGL\n");
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    FrameEditor editor;
    std::string status = "Load a media file to start";

    char input_path[1024] = {0};
    char output_path[1024] = {0};

    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                done = true;
            }
            if (event.type == SDL_DROPFILE) {
                std::string err;
                if (editor.Load(event.drop.file, err)) {
                    status = "Loaded packets: " + std::to_string(editor.EditCount());
                    strncpy(input_path, event.drop.file, sizeof(input_path) - 1);
                } else {
                    status = "Load failed: " + err;
                }
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window)) {
                done = true;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        DrawEditorUI(editor, input_path, sizeof(input_path), output_path, sizeof(output_path), status);
        ImGui::ShowDemoWindow();
        ImGui::Render();

        int display_w = 0;
        int display_h = 0;
        SDL_GL_GetDrawableSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
