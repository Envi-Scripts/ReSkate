#include "Extension/Trainer/trainer_page.h"
#include "Extension/Trainer/trainer_presets.h"
#include "Extension/UI/Overlay/skate_menu_internal.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
namespace physics=dingosdk::trainer;
using namespace dingosdk::overlay;
void require(bool good,const char *message){if(!good)throw std::runtime_error(message);}
// Optional CPU rendering of the actual native draw data, with no game/window/input automation.
void image(const std::filesystem::path &path, const ImDrawData &data, const unsigned char *atlas, int atlas_width, int atlas_height) {
    const int width = static_cast<int>(data.DisplaySize.x), height = static_cast<int>(data.DisplaySize.y);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3, 18);
    const auto edge = [](ImVec2 a, ImVec2 b, ImVec2 p) { return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x); };
    for (const auto *list : data.CmdLists) for (const auto &command : list->CmdBuffer) {
        if (command.UserCallback) continue;
        const int clip_left = std::max(0, static_cast<int>(command.ClipRect.x)), clip_top = std::max(0, static_cast<int>(command.ClipRect.y));
        const int clip_right = std::min(width, static_cast<int>(command.ClipRect.z)), clip_bottom = std::min(height, static_cast<int>(command.ClipRect.w));
        for (unsigned int i = 0; i + 2 < command.ElemCount; i += 3) {
            const auto &a = list->VtxBuffer[list->IdxBuffer[command.IdxOffset + i] + command.VtxOffset];
            const auto &b = list->VtxBuffer[list->IdxBuffer[command.IdxOffset + i + 1] + command.VtxOffset];
            const auto &c = list->VtxBuffer[list->IdxBuffer[command.IdxOffset + i + 2] + command.VtxOffset];
            const auto area = edge(a.pos, b.pos, c.pos);
            if (std::abs(area) < 1e-6f) continue;
            const int left = std::max(clip_left, static_cast<int>(std::floor(std::min({a.pos.x, b.pos.x, c.pos.x}))));
            const int right = std::min(clip_right, static_cast<int>(std::ceil(std::max({a.pos.x, b.pos.x, c.pos.x}))));
            const int top = std::max(clip_top, static_cast<int>(std::floor(std::min({a.pos.y, b.pos.y, c.pos.y}))));
            const int bottom = std::min(clip_bottom, static_cast<int>(std::ceil(std::max({a.pos.y, b.pos.y, c.pos.y}))));
            for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
                const ImVec2 point(static_cast<float>(x) + .5f, static_cast<float>(y) + .5f);
                const float wa = edge(b.pos, c.pos, point) / area, wb = edge(c.pos, a.pos, point) / area, wc = 1 - wa - wb;
                if (wa < 0 || wb < 0 || wc < 0) continue;
                const auto u = wa * a.uv.x + wb * b.uv.x + wc * c.uv.x, v = wa * a.uv.y + wb * b.uv.y + wc * c.uv.y;
                const int tx = std::clamp(static_cast<int>(u * static_cast<float>(atlas_width)), 0, atlas_width - 1);
                const int ty = std::clamp(static_cast<int>(v * static_cast<float>(atlas_height)), 0, atlas_height - 1);
                const auto texture = static_cast<std::size_t>(ty * atlas_width + tx) * 4;
                const auto component = [&](unsigned shift) {
                    return wa * static_cast<float>((a.col >> shift) & 255u) + wb * static_cast<float>((b.col >> shift) & 255u) + wc * static_cast<float>((c.col >> shift) & 255u);
                };
                const float alpha = component(IM_COL32_A_SHIFT) / 255 * static_cast<float>(atlas[texture + 3]) / 255;
                const auto at = static_cast<std::size_t>(y * width + x) * 3;
                const unsigned shifts[]{IM_COL32_R_SHIFT, IM_COL32_G_SHIFT, IM_COL32_B_SHIFT};
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    const float color = component(shifts[channel]) * static_cast<float>(atlas[texture + channel]) / 255;
                    pixels[at + channel] = static_cast<unsigned char>(std::clamp(color * alpha + static_cast<float>(pixels[at + channel]) * (1 - alpha), 0.0f, 255.0f));
                }
            }
        }
    }
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << width << ' ' << height << "\n255\n";
    output.write(reinterpret_cast<const char *>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    require(static_cast<bool>(output), "Could not write native UI preview");
}

struct NativeItem { ImRect rectangle; ImGuiWindow *window{}; std::string label; int frame{}; int submissions{}; bool disabled{}; };
std::map<ImGuiID, NativeItem> native_items;
void ImGuiTestEngineHook_ItemAdd(ImGuiContext *context, ImGuiID id, const ImRect &rectangle, const ImGuiLastItemData *data) {
    auto &item = native_items[id];
    if (item.frame != context->FrameCount) item.submissions = 0;
    ++item.submissions; item.rectangle = rectangle; item.window = context->CurrentWindow; item.frame = context->FrameCount;
    item.disabled = data && (data->ItemFlags & ImGuiItemFlags_Disabled);
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext *, ImGuiID id, const char *label, ImGuiItemStatusFlags) { native_items[id].label = label ? label : ""; }
void ImGuiTestEngineHook_Log(ImGuiContext *, const char *, ...) {}
const char *ImGuiTestEngine_FindItemDebugLabel(ImGuiContext *, ImGuiID id) {
    const auto item = native_items.find(id); return item == native_items.end() ? "" : item->second.label.c_str();
}
NativeItem native_item(const char *label) {
    auto *context = ImGui::GetCurrentContext();
    for (const auto &[id, item] : native_items)
        if (item.frame == context->FrameCount && item.label == label && item.window && item.window->Active && item.window->ClipRect.Contains(item.rectangle.GetCenter())) return item;
    // BeginCombo has ItemAdd but no ItemInfo in this ImGui version. Plain controls
    // have stable root IDs in active workspace, modal and inspector windows.
    for (auto *window : context->Windows) if (window->Active) {
        const auto item = native_items.find(window->GetID(label));
        if (item != native_items.end() && item->second.frame == context->FrameCount) return item->second;
    }
    throw std::runtime_error(std::string("Native widget was not submitted: ") + label);
}
int main(int argc, char **argv) {
    try {
        ImGui::CreateContext();
        auto &io = ImGui::GetIO(); io.IniFilename = nullptr; io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        GImGui->TestEngineHookItems = true;
        SkateMenu skate; load_skate_fonts(skate); skate.page = 4;
        Model model; model.steam_offline = true;
        std::vector<std::string> queued;
        CallbacksV3 callbacks{}; callbacks.user = &queued;
        callbacks.queue_console_command = [](void *user, const char *text, char *out, std::size_t size) {
            static_cast<std::vector<std::string> *>(user)->emplace_back(text);
            if(size) out[0] = 0; return true;
        };
        auto view = std::make_shared<physics::View>();
        view->ready = view->editable = view->capture_changed = true; view->map = "test-map";
        const auto add = [&](const char *id, const char *label, double stock) {
            physics::Row row; row.id = id; row.label = row.friendly = label; row.group = "Physics";
            row.used = true; row.stock = row.value = stock; row.help = "Known fixture control with a long description for readable layout checks.";
            view->rows.push_back(row);
        };
        add("physicsmode.grindlockdist", "Grind lock-on distance", .9f);
        add("physicsgrindsair.maxdistboardslide", "Board slide capture distance", .75f);
        add("physicsgrindsair.maxdisttipslide", "Nose and tail slide capture distance", .7f);
        add("physicsgrind.commonfrictionscalar", "Grind friction", .35f);
        add("physicsmode.jumpmaxheight", "Ollie height", 1.575f);
        add("onboard_powerslide.frictionscalar_revert", "Revert friction", 3.0f);
        add("physicsmode.grindjumpcommonmax", "Maximum pop out of a grind", 2.3f);
        add("physicsmode.deepcrouch", "Landing: leg length (lower crouches deeper; deliberately long label)", .65f);
        view->groups.push_back("Physics");
        for(const auto &source : physics::builtin_presets()) {
            physics::PresetRow row; row.name=source.name; row.note=source.note; row.builtin=true;
            const auto dial=physics::preset_dial(source.name); row.title=dial.title; row.dial=!dial.title.empty();
            row.factor=1; row.amount=source.rules.empty()?1:source.rules.front().amount;
            view->presets.push_back(std::move(row));
        }
        view->presets.push_back({"Envi Hardcore - preserved", "Your previous values stay separate from the new Hardcore default.", false, false});
        view->presets.back().values = {{"physicsmode.grindlockdist", .4050000011920929}, {"trick.flip_speed", .8}, {"unknown.old-game-value", 2}};
        unsigned char *atlas{}; int atlas_width{}, atlas_height{};
        io.Fonts->GetTexDataAsRGBA32(&atlas,&atlas_width,&atlas_height);
        bool visible=true;
        float width=1000, height=850, scale=1;
        const auto frame = [&] {
            io.DisplaySize=ImVec2(width+40,(height+150)*scale); io.DeltaTime=1.0f/60;
            model.menu_scale=scale; physics::publish(view);
            ImGui::NewFrame();
            // Production first-use defaults replace pending SetNextWindowSize data.
            // Resize the existing named fixture window directly on later frames.
            ImGui::SetWindowSize("ReSkate###skate-menu",ImVec2(width,height*scale),ImGuiCond_Always);
            draw_skate_menu(skate,model,callbacks,visible); ImGui::Render();
        };
        const auto click = [&](const char *label) {
            const auto item=native_item(label); require(!item.disabled,"Click target must be enabled");
            const auto centre=item.rectangle.GetCenter();
            if (!item.window->ClipRect.Contains(centre)) throw std::runtime_error(std::string("Click target must be visible: ")+label);
            io.AddMousePosEvent(centre.x,centre.y); io.AddMouseButtonEvent(0,true); frame();
            io.AddMouseButtonEvent(0,false); frame(); frame();
        };
        const auto save = [&](const char *name) {
            if(argc<2)return;
            const auto directory=std::filesystem::path(argv[1]); std::filesystem::create_directories(directory);
            image(directory/name,*ImGui::GetDrawData(),atlas,atlas_width,atlas_height);
        };
        frame(); frame();
        require(native_item("FEEL").rectangle.GetWidth()>100,"Primary navigation has readable targets");
        for(const auto *label:{"Hardcore","Authentic","Stock","Accessible","Arcade"}) native_item(label);
        click("Hardcore"); require(queued.empty(),"Choosing Hardcore must only change the preview");
        click("Apply this feel"); require(queued.size()==1 && queued.back()=="trainer workshop -1","Explicit Apply queues the selected feel");
        queued.clear(); save("feel-wide.ppm");
        click("FUN"); native_item("Super Ollie");
        require(queued.empty(),"Navigating to Fun applies no shortcut"); save("fun-wide.ppm");
        // Submit both expanded cards together. A tall headless viewport keeps the
        // real widgets visible, so clipping cannot hide a duplicate submission.
        height=3600; frame(); frame();
        click("Tricklining and reverts"); click("Trick height and flip settings");
        click("What counts as a bend, and what it is worth"); frame();
        const auto widgets_once = [&](ImGuiWindow *window, const char *label, int expected) {
            int count=0;
            for (const auto &[id,item] : native_items)
                if (item.frame==GImGui->FrameCount && item.window==window && item.label==label) {
                    require(id!=0 && item.submissions==1,"Each real widget ID must be submitted exactly once per frame");
                    ++count;
                }
            if (count!=expected) throw std::runtime_error(std::string("Expanded card widget count: ")+label+" expected "+std::to_string(expected)+", got "+std::to_string(count));
        };
        auto *tricks=native_item("Let slow flips stay slow").window;
        require(tricks->BeginCount==1,"Both FUN sections must render the TRICKS card only once");
        widgets_once(tricks,"##height",5); widgets_once(tricks,"##typed",6);
        widgets_once(tricks,"##strength",1); widgets_once(tricks,"Let slow flips stay slow",1);
        widgets_once(tricks,"Reset tricks",1);
        auto *reverts=native_item("Reset these rules").window;
        require(reverts!=tricks && reverts->BeginCount==1,"Revert rules retain their own single card");
        widgets_once(reverts,"##value",11); widgets_once(reverts,"##typed",10);
        widgets_once(reverts,"##freeze",1); widgets_once(reverts,"Reset these rules",1);
        require(queued.empty(),"Expanding both FUN sections does not change any setting");
        click("Reset these rules");
        require(queued.size()==1 && queued.back()=="trainer revert reset","The surviving revert reset queues its exact command once");
        queued.clear(); click("Reset tricks");
        require(queued.size()==1 && queued.back()=="trainer reset tricks","The single TRICKS reset remains functional");
        queued.clear();
        click("Trick height and flip settings"); click("Tricklining and reverts");
        height=850; frame(); frame();
        click("SETTINGS");
        auto scalar=native_item("##value");
        require(scalar.rectangle.GetWidth()>600,"Settings use the full content width below labels"); save("settings-wide.ppm");
        // The temporary text editor must keep exact precision and cancel without a command.
        const auto type_scalar = [&](const char *text, bool commit) {
            const auto item=native_item("##value"); const auto centre=item.rectangle.GetCenter();
            io.AddMousePosEvent(centre.x,centre.y); io.AddKeyEvent(ImGuiMod_Ctrl,true); frame();
            io.AddMouseButtonEvent(0,true); frame(); io.AddMouseButtonEvent(0,false); frame();
            io.AddKeyEvent(ImGuiMod_Ctrl,false); frame();
            require(GImGui->InputTextState.ID != 0 && ImGui::TempInputIsActive(GImGui->InputTextState.ID),"Ctrl-click opens the actual precise scalar editor");
            io.AddInputCharactersUTF8(text); frame();
            const auto key=commit?ImGuiKey_Enter:ImGuiKey_Escape;
            io.AddKeyEvent(key,true); frame(); io.AddKeyEvent(key,false); frame(); frame();
        };
        type_scalar("0.4050000011920929",false);
        require(queued.empty(),"Escape cancels a typed scalar without applying any setting");
        type_scalar("0.4050000011920929",true);
        require(queued.size()==1 && queued.back().starts_with("trainer set ") && queued.back().ends_with(" 0.4050000011920929"),"Enter commits the precise value exactly once");
        queued.clear();
        click("PRESETS"); native_item("Save current setup");
        click("Review preset"); require(queued.empty(),"Reviewing a saved preset applies no physics");
        native_item("Apply reviewed preset"); save("preset-review.ppm");
        click("Apply reviewed preset");
        require(queued.size()==1 && queued.back()=="trainer preset apply Envi Hardcore - preserved","Reviewed preset uses the exact saved name");
        queued.clear();
        view->capture_changed=false; ++view->revision; frame();
        require(native_item("Save current setup").disabled,"Unchanged setup cannot be saved");
        // A trick-only change enables Save after entering a name, without a touched physics row.
        view->capture_changed=true; view->touched=0; ++view->revision;
        click("##preset-name"); io.AddInputCharactersUTF8("Tricks only"); frame();
        require(!native_item("Save current setup").disabled,"Script-only edits can be saved");
        click("Save current setup"); require(queued.back()=="trainer preset save Tricks only","Save queues the exact name");
        queued.clear(); save("presets-wide.ppm");
        click("FEEL");
        width=750; frame(); frame(); save("feel-narrow.ppm");
        for(const auto *label:{"FEEL","SETTINGS","PRESETS","FUN","TOOLS"}) {
            const auto item=native_item(label);
            require(item.rectangle.Min.x>=item.window->InnerClipRect.Min.x && item.rectangle.Max.x<=item.window->InnerClipRect.Max.x+1,"Primary navigation stays inside narrow content");
        }
        click("SETTINGS"); frame(); scalar=native_item("##value");
        require(scalar.rectangle.GetWidth()>400,"Narrow settings retain roomy full-width editors"); save("settings-narrow.ppm");
        click("FEEL"); scale=2; width=1500; frame(); frame(); save("feel-2x.ppm");
        click("TOOLS"); for(const auto *label:{"PRACTICE","CAMERA","MAP & HUD"}) native_item(label);
        require(queued.empty(),"Layout changes and navigation never queue physics mutations");
        click("FEEL"); view->editable=false; view->blocked="The host controls physics."; ++view->revision; frame();
        require(native_item("Apply this feel").disabled,"Guest cannot apply a feel under host control");
        require(ImGui::GetDrawData()->TotalVtxCount>0,"Real native widgets produce draw data");
        ImGui::DestroyContext();
        std::cout<<"Native workshop navigation, preview/apply, preset saving, widths and host guards passed\n";
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
