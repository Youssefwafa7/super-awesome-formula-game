#pragma once

#include <application.hpp>
#include <shader/shader.hpp>
#include <texture/texture2d.hpp>
#include <texture/texture-utils.hpp>
#include <material/material.hpp>
#include <mesh/mesh.hpp>

#include <imgui_impl/imgui_impl_opengl3.h>
#include "miniaudio.h"

#include <functional>
#include <array>
#include <algorithm>
#include <string>
#include <vector>
#include <cstdio>

// This struct is used to store the location and size of a button and the code it should execute when clicked
struct Button
{
    // The position (of the top-left corner) of the button and its size in pixels
    glm::vec2 position, size;
    // The function that should be excuted when the button is clicked. It takes no arguments and returns nothing.
    std::function<void()> action;

    // This function returns true if the given vector v is inside the button. Otherwise, false is returned.
    // This is used to check if the mouse is hovering over the button.
    bool isInside(const glm::vec2 &v) const
    {
        return position.x <= v.x && position.y <= v.y &&
               v.x <= position.x + size.x &&
               v.y <= position.y + size.y;
    }

    // This function returns the local to world matrix to transform a rectangle of size 1x1
    // (and whose top-left corner is at the origin) to be the button.
    glm::mat4 getLocalToWorld() const
    {
        return glm::translate(glm::mat4(1.0f), glm::vec3(position.x, position.y, 0.0f)) *
               glm::scale(glm::mat4(1.0f), glm::vec3(size.x, size.y, 1.0f));
    }
};

// ── F1 Menu Screen Enum ──
enum class MenuScreen
{
    MAIN_MENU,
    TRACK_SELECT,
    CAR_SELECT
};

// This state shows how to use some of the abstractions we created to make a menu.
class Menustate : public our::State
{

    // A meterial holding the menu shader and the menu texture to draw
    our::TexturedMaterial *menuMaterial;
    // A material to be used to highlight hovered buttons (we will use blending to create a negative effect).
    our::TintedMaterial *highlightMaterial;
    // A rectangle mesh on which the menu material will be drawn
    our::Mesh *rectangle;
    // A variable to record the time since the state is entered (it will be used for the fading effect).
    float time;
    // An array of the button that we can interact with
    std::array<Button, 2> buttons;

    // ── Audio ──
    ma_engine audioEngine;
    ma_sound menuMusic;
    bool isAudioInitialized = false;
    bool isMusicLoaded = false;

    // ── Fonts (loaded once, crisp at native size) ──
    ImFont* titleFont  = nullptr;  // ~48px bold
    ImFont* headingFont = nullptr; // ~32px bold
    ImFont* bodyFont   = nullptr;  // ~20px bold

    // ── Preset data ──
    std::vector<std::string> carPresetIds;
    std::vector<std::string> carPresetLabels;
    std::vector<std::string> trackPresetIds;
    std::vector<std::string> trackPresetLabels;
    int selectedCarIndex = 0;
    int selectedTrackIndex = 0;

    // ── ImGui menu state ──
    MenuScreen currentScreen = MenuScreen::MAIN_MENU;

    // ── F1 Theme Colors ──
    static ImVec4 kBgColor() { return ImVec4(0.04f, 0.04f, 0.04f, 0.00f); }   // #0A0A0A transparent
    static ImVec4 kRedAccent() { return ImVec4(0.91f, 0.00f, 0.18f, 1.00f); } // #E8002D
    static ImVec4 kRedHover() { return ImVec4(1.00f, 0.15f, 0.30f, 1.00f); }
    static ImVec4 kRedActive() { return ImVec4(0.70f, 0.00f, 0.12f, 1.00f); }
    static ImVec4 kCardBg() { return ImVec4(0.10f, 0.10f, 0.10f, 1.00f); }
    static ImVec4 kCardSelected() { return ImVec4(0.20f, 0.04f, 0.06f, 1.00f); }
    static ImVec4 kTextWhite() { return ImVec4(1.0f, 1.0f, 1.0f, 1.0f); }
    static ImVec4 kTextDim() { return ImVec4(0.6f, 0.6f, 0.6f, 1.0f); }

    void loadPresetsFromConfig()
    {
        carPresetIds.clear();
        carPresetLabels.clear();
        trackPresetIds.clear();
        trackPresetLabels.clear();

        const auto &cfg = getApp()->getConfig();
        if (!cfg.contains("scene"))
            return;
        const auto &scene = cfg["scene"];
        if (!scene.contains("presets"))
            return;
        const auto &presets = scene["presets"];

        if (presets.contains("cars") && presets["cars"].is_array())
        {
            for (const auto &c : presets["cars"])
            {
                const std::string id = c.value("id", std::string{});
                if (id.empty())
                    continue;
                carPresetIds.push_back(id);
                carPresetLabels.push_back(c.value("label", id));
            }
        }

        if (presets.contains("tracks") && presets["tracks"].is_array())
        {
            for (const auto &t : presets["tracks"])
            {
                const std::string id = t.value("id", std::string{});
                if (id.empty())
                    continue;
                trackPresetIds.push_back(id);
                trackPresetLabels.push_back(t.value("label", id));
            }
        }

        const std::string defaultCar = (scene.contains("selection") ? scene["selection"].value("car", std::string{}) : std::string{});
        const std::string defaultTrack = (scene.contains("selection") ? scene["selection"].value("track", std::string{}) : std::string{});

        auto findIndex = [](const std::vector<std::string> &ids, const std::string &needle)
        {
            for (int i = 0; i < (int)ids.size(); i++)
                if (ids[i] == needle)
                    return i;
            return 0;
        };

        if (!carPresetIds.empty())
            selectedCarIndex = findIndex(carPresetIds, defaultCar);
        if (!trackPresetIds.empty())
            selectedTrackIndex = findIndex(trackPresetIds, defaultTrack);

        if (!carPresetIds.empty())
            getApp()->setSelectedCarPreset(carPresetIds[selectedCarIndex]);
        if (!trackPresetIds.empty())
            getApp()->setSelectedTrackPreset(trackPresetIds[selectedTrackIndex]);
    }

    // ── Helper: push the F1 dark theme styles ──
    int pushF1Theme()
    {
        int colorCount = 0;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, kBgColor());
        colorCount++;
        ImGui::PushStyleColor(ImGuiCol_Button, kRedAccent());
        colorCount++;
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kRedHover());
        colorCount++;
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kRedActive());
        colorCount++;
        ImGui::PushStyleColor(ImGuiCol_Text, kTextWhite());
        colorCount++;
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        colorCount++;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kCardBg());
        colorCount++;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 20.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 10.0f));
        return colorCount;
    }

    void popF1Theme(int colorCount)
    {
        ImGui::PopStyleVar(5);
        ImGui::PopStyleColor(colorCount);
    }

    // ── Helper: centered text ──
    static void textCentered(const char *text)
    {
        float w = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - w) * 0.5f);
        ImGui::Text("%s", text);
    }

    // ── Helper: centered text with red glow (illuminate effect) ──
    static void textCenteredGlow(const char *text, const ImVec4& glowColor)
    {
        float w = ImGui::CalcTextSize(text).x;
        float x = (ImGui::GetWindowWidth() - w) * 0.5f;
        ImGui::SetCursorPosX(x);

        ImVec2 screenPos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* font = ImGui::GetFont();
        float fontSize = ImGui::GetFontSize();

        // Draw glow layers (expanding offsets with decreasing alpha)
        const float offsets[] = {4.0f, 3.0f, 2.0f, 1.0f};
        const float alphas[]  = {0.06f, 0.10f, 0.15f, 0.25f};
        for(int i = 0; i < 4; i++) {
            float off = offsets[i];
            ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(glowColor.x, glowColor.y, glowColor.z, alphas[i]));
            dl->AddText(font, fontSize, ImVec2(screenPos.x - off, screenPos.y), col, text);
            dl->AddText(font, fontSize, ImVec2(screenPos.x + off, screenPos.y), col, text);
            dl->AddText(font, fontSize, ImVec2(screenPos.x, screenPos.y - off), col, text);
            dl->AddText(font, fontSize, ImVec2(screenPos.x, screenPos.y + off), col, text);
        }

        // Draw main white text on top
        dl->AddText(font, fontSize, screenPos, IM_COL32(255, 255, 255, 255), text);

        // Advance cursor past the text
        ImGui::Dummy(ImVec2(w, fontSize));
    }

    // ── Helper: centered button, returns true if clicked ──
    static bool buttonCentered(const char *label, const ImVec2 &size = ImVec2(220, 50))
    {
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - size.x) * 0.5f);
        return ImGui::Button(label, size);
    }

    // ── Screen: Main Menu ──
    void renderMainMenu()
    {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(display);

        int colors = pushF1Theme();
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("##MainMenu", nullptr, flags))
        {
            // Vertical centering
            float contentH = 80.0f + 40.0f + 40.0f + 65.0f + 65.0f + 30.0f;
            ImGui::SetCursorPosY((display.y - contentH) * 0.5f);

            // Title with glow
            if(titleFont) ImGui::PushFont(titleFont);
            textCenteredGlow("SUPER AWESOME FORMULA GAME", kRedAccent());
            if(titleFont) ImGui::PopFont();
            if(bodyFont) ImGui::PushFont(bodyFont);

            ImGui::Dummy(ImVec2(0, 40));

            // Subtitle
            ImGui::PushStyleColor(ImGuiCol_Text, kTextDim());
            textCentered("Choose your track and car, then race!");
            ImGui::PopStyleColor();

            ImGui::Dummy(ImVec2(0, 40));

            // PLAY button
            if (buttonCentered("PLAY", ImVec2(260, 55)))
            {
                currentScreen = MenuScreen::TRACK_SELECT;
            }
            ImGui::Dummy(ImVec2(0, 8));

            // EXIT button (styled differently)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
            if (buttonCentered("EXIT", ImVec2(260, 55)))
            {
                getApp()->close();
            }
            ImGui::PopStyleColor(3);
            if(bodyFont) ImGui::PopFont();
        }
        ImGui::End();
        popF1Theme(colors);
    }

    // ── Screen: Track Selection ──
    void renderTrackSelect()
    {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(display);

        int colors = pushF1Theme();
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("##TrackSelect", nullptr, flags))
        {
            // Vertical centering
            float contentH = 50.0f + 30.0f + 140.0f + 50.0f + 55.0f;
            ImGui::SetCursorPosY((display.y - contentH) * 0.5f);

            if(headingFont) ImGui::PushFont(headingFont);
            textCenteredGlow("SELECT TRACK", kRedAccent());
            if(headingFont) ImGui::PopFont();
            if(bodyFont) ImGui::PushFont(bodyFont);
            ImGui::Dummy(ImVec2(0, 30));

            // Draw track cards side by side
            const char *trackNames[] = {"Montreal", "Silverstone"};
            const int trackCount = (int)trackPresetIds.size();
            const int displayCount = std::min(trackCount, 2);

            float cardW = 280.0f;
            float cardH = 120.0f;
            float gap = 40.0f;
            float totalW = displayCount * cardW + (displayCount - 1) * gap;
            float startX = (ImGui::GetWindowWidth() - totalW) * 0.5f;

            for (int i = 0; i < displayCount; i++)
            {
                ImGui::SetCursorPosX(startX + i * (cardW + gap));
                if (i > 0)
                    ImGui::SameLine();

                bool isSelected = (selectedTrackIndex == i);
                const char *name = (i < 2) ? trackNames[i] : trackPresetLabels[i].c_str();

                // Card styling
                if (isSelected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, kCardSelected());
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kCardSelected());
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kCardSelected());
                    ImGui::PushStyleColor(ImGuiCol_Border, kRedAccent());
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, kCardBg());
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kCardBg());
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                }

                // Build card label with newlines for visual layout
                char cardLabel[128];
                snprintf(cardLabel, sizeof(cardLabel), "\n  %s\n\n  %s\n", name, isSelected ? "[SELECTED]" : "Click to select");

                char btnId[64];
                snprintf(btnId, sizeof(btnId), "%s##track%d", cardLabel, i);

                if (ImGui::Button(btnId, ImVec2(cardW, cardH)))
                {
                    selectedTrackIndex = i;
                    if (i < trackCount)
                        getApp()->setSelectedTrackPreset(trackPresetIds[i]);
                }

                ImGui::PopStyleVar(1);
                ImGui::PopStyleColor(4);
            }

            ImGui::Dummy(ImVec2(0, 40));

            // Navigation buttons
            float navW = 160.0f;
            float navGap = 20.0f;
            float navStartX = (ImGui::GetWindowWidth() - 2 * navW - navGap) * 0.5f;

            ImGui::SetCursorPosX(navStartX);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
            if (ImGui::Button("< BACK", ImVec2(navW, 45)))
            {
                currentScreen = MenuScreen::MAIN_MENU;
            }
            ImGui::PopStyleColor(3);

            ImGui::SameLine(0, navGap);
            if (ImGui::Button("NEXT >", ImVec2(navW, 45)))
            {
                currentScreen = MenuScreen::CAR_SELECT;
            }
            if(bodyFont) ImGui::PopFont();
        }
        ImGui::End();
        popF1Theme(colors);
    }

    // ── Screen: Car Selection ──
    void renderCarSelect()
    {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(display);

        int colors = pushF1Theme();
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("##CarSelect", nullptr, flags))
        {
            // Vertical centering
            float contentH = 50.0f + 30.0f + 140.0f + 50.0f + 55.0f;
            ImGui::SetCursorPosY((display.y - contentH) * 0.5f);

            if(headingFont) ImGui::PushFont(headingFont);
            textCenteredGlow("SELECT CAR", kRedAccent());
            if(headingFont) ImGui::PopFont();
            if(bodyFont) ImGui::PushFont(bodyFont);
            ImGui::Dummy(ImVec2(0, 30));

            const char *carNames[] = {"New Ferrari", "Old Ferrari"};
            const int carCount = (int)carPresetIds.size();
            const int displayCount = std::min(carCount, 2);

            float cardW = 280.0f;
            float cardH = 120.0f;
            float gap = 40.0f;
            float totalW = displayCount * cardW + (displayCount - 1) * gap;
            float startX = (ImGui::GetWindowWidth() - totalW) * 0.5f;

            for (int i = 0; i < displayCount; i++)
            {
                ImGui::SetCursorPosX(startX + i * (cardW + gap));
                if (i > 0)
                    ImGui::SameLine();

                bool isSelected = (selectedCarIndex == i);
                const char *name = (i < 2) ? carNames[i] : carPresetLabels[i].c_str();

                if (isSelected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, kCardSelected());
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kCardSelected());
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kCardSelected());
                    ImGui::PushStyleColor(ImGuiCol_Border, kRedAccent());
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, kCardBg());
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kCardBg());
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                }

                char cardLabel[128];
                snprintf(cardLabel, sizeof(cardLabel), "\n  %s\n\n  %s\n", name, isSelected ? "[SELECTED]" : "Click to select");
                char btnId[64];
                snprintf(btnId, sizeof(btnId), "%s##car%d", cardLabel, i);

                if (ImGui::Button(btnId, ImVec2(cardW, cardH)))
                {
                    selectedCarIndex = i;
                    if (i < carCount)
                        getApp()->setSelectedCarPreset(carPresetIds[i]);
                }

                ImGui::PopStyleVar(1);
                ImGui::PopStyleColor(4);
            }

            ImGui::Dummy(ImVec2(0, 40));

            // Navigation buttons
            float navW = 160.0f;
            float navGap = 20.0f;
            float navStartX = (ImGui::GetWindowWidth() - 2 * navW - navGap) * 0.5f;

            ImGui::SetCursorPosX(navStartX);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
            if (ImGui::Button("< BACK##car", ImVec2(navW, 45)))
            {
                currentScreen = MenuScreen::TRACK_SELECT;
            }
            ImGui::PopStyleColor(3);

            ImGui::SameLine(0, navGap);
            if (ImGui::Button("START RACE", ImVec2(navW, 45)))
            {
                getApp()->changeState("play");
            }
            if(bodyFont) ImGui::PopFont();
        }
        ImGui::End();
        popF1Theme(colors);
    }

    void loadFonts() {
        static bool fontsLoaded = false;
        if(fontsLoaded) return;
        fontsLoaded = true;

        ImGuiIO& io = ImGui::GetIO();

        // Add the small default font FIRST so it stays the default for in-game HUDs
        io.Fonts->AddFontDefault();

        ImFontConfig cfg;
        cfg.OversampleH = 3;
        cfg.OversampleV = 2;

        // Try to find a clean TTF font on the system
        const char* fontPaths[] = {
            "/usr/share/fonts/google-noto/NotoSans-ExtraBold.ttf",
            "/usr/share/fonts/google-noto/NotoSans-Bold.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
            nullptr
        };

        const char* fontPath = nullptr;
        for(int i = 0; fontPaths[i]; i++) {
            FILE* f = fopen(fontPaths[i], "rb");
            if(f) { fclose(f); fontPath = fontPaths[i]; break; }
        }

        if(fontPath) {
            titleFont   = io.Fonts->AddFontFromFileTTF(fontPath, 48.0f, &cfg);
            headingFont = io.Fonts->AddFontFromFileTTF(fontPath, 32.0f, &cfg);
            bodyFont    = io.Fonts->AddFontFromFileTTF(fontPath, 20.0f, &cfg);
        } else {
            // Fallback: default font at larger sizes (still better than scaling)
            cfg.SizePixels = 48.0f;
            titleFont = io.Fonts->AddFontDefault(&cfg);
            cfg.SizePixels = 32.0f;
            headingFont = io.Fonts->AddFontDefault(&cfg);
            cfg.SizePixels = 20.0f;
            bodyFont = io.Fonts->AddFontDefault(&cfg);
        }

        // Rebuild the font atlas texture
        io.Fonts->Build();
        ImGui_ImplOpenGL3_DestroyFontsTexture();
        ImGui_ImplOpenGL3_CreateFontsTexture();
    }

    void onInitialize() override
    {
        loadFonts();
        loadPresetsFromConfig();
        currentScreen = MenuScreen::MAIN_MENU;

        // First, we create a material for the menu's background
        menuMaterial = new our::TexturedMaterial();
        // Here, we load the shader that will be used to draw the background
        menuMaterial->shader = new our::ShaderProgram();
        menuMaterial->shader->attach("assets/shaders/textured.vert", GL_VERTEX_SHADER);
        menuMaterial->shader->attach("assets/shaders/textured.frag", GL_FRAGMENT_SHADER);
        menuMaterial->shader->link();
        // Then we load the menu texture
        menuMaterial->texture = our::texture_utils::loadImage("assets/textures/background.png");
        // Initially, the menu material will be black, then it will fade in
        menuMaterial->tint = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

        // Second, we create a material to highlight the hovered buttons
        highlightMaterial = new our::TintedMaterial();
        // Since the highlight is not textured, we used the tinted material shaders
        highlightMaterial->shader = new our::ShaderProgram();
        highlightMaterial->shader->attach("assets/shaders/tinted.vert", GL_VERTEX_SHADER);
        highlightMaterial->shader->attach("assets/shaders/tinted.frag", GL_FRAGMENT_SHADER);
        highlightMaterial->shader->link();
        // The tint is white since we will subtract the background color from it to create a negative effect.
        highlightMaterial->tint = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        // To create a negative effect, we enable blending, set the equation to be subtract,
        // and set the factors to be one for both the source and the destination.
        highlightMaterial->pipelineState.blending.enabled = true;
        highlightMaterial->pipelineState.blending.equation = GL_FUNC_SUBTRACT;
        highlightMaterial->pipelineState.blending.sourceFactor = GL_ONE;
        highlightMaterial->pipelineState.blending.destinationFactor = GL_ONE;

        // Then we create a rectangle whose top-left corner is at the origin and its size is 1x1.
        // Note that the texture coordinates at the origin is (0.0, 1.0) since we will use the
        // projection matrix to make the origin at the the top-left corner of the screen.
        rectangle = new our::Mesh({
                                      {{0.0f, 0.0f, 0.0f}, {255, 255, 255, 255}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
                                      {{1.0f, 0.0f, 0.0f}, {255, 255, 255, 255}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
                                      {{1.0f, 1.0f, 0.0f}, {255, 255, 255, 255}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
                                      {{0.0f, 1.0f, 0.0f}, {255, 255, 255, 255}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
                                  },
                                  {
                                      0,
                                      1,
                                      2,
                                      2,
                                      3,
                                      0,
                                  });

        // Reset the time elapsed since the state is entered.
        time = 0;

        // Fill the positions, sizes and actions for the menu buttons
        // Note that we use lambda expressions to set the actions of the buttons.
        buttons[0].position = {830.0f, 607.0f};
        buttons[0].size = {400.0f, 33.0f};
        buttons[0].action = [this]()
        { this->getApp()->changeState("play"); };

        buttons[1].position = {830.0f, 644.0f};
        buttons[1].size = {400.0f, 33.0f};
        buttons[1].action = [this]()
        { this->getApp()->close(); };

        // Initialize Audio Engine and start music
        if (ma_engine_init(NULL, &audioEngine) == MA_SUCCESS) {
            isAudioInitialized = true;
            if (ma_sound_init_from_file(&audioEngine, "assets/audio/01. Ground Theme.mp3", 0, NULL, NULL, &menuMusic) == MA_SUCCESS) {
                isMusicLoaded = true;
                ma_sound_set_looping(&menuMusic, MA_TRUE);
                ma_sound_start(&menuMusic);
            }
        }
    }

    void onImmediateGui() override
    {
        // Disable imgui.ini saving so windows don't remember position
        ImGui::GetIO().IniFilename = nullptr;

        switch (currentScreen)
        {
        case MenuScreen::MAIN_MENU:
            renderMainMenu();
            break;
        case MenuScreen::TRACK_SELECT:
            renderTrackSelect();
            break;
        case MenuScreen::CAR_SELECT:
            renderCarSelect();
            break;
        }
    }

    void onDraw(double deltaTime) override
    {
        // Get a reference to the keyboard object
        auto &keyboard = getApp()->getKeyboard();

        if (keyboard.justPressed(GLFW_KEY_ESCAPE))
        {
            if (currentScreen == MenuScreen::CAR_SELECT)
            {
                currentScreen = MenuScreen::TRACK_SELECT;
            }
            else if (currentScreen == MenuScreen::TRACK_SELECT)
            {
                currentScreen = MenuScreen::MAIN_MENU;
            }
            else
            {
                getApp()->close();
            }
        }

        // Get the framebuffer size to set the viewport and the create the projection matrix.
        glm::ivec2 size = getApp()->getFrameBufferSize();
        // Make sure the viewport covers the whole size of the framebuffer.
        glViewport(0, 0, size.x, size.y);

        // The view matrix is an identity (there is no camera that moves around).
        // The projection matrix applys an orthographic projection whose size is the framebuffer size in pixels
        glm::mat4 VP = glm::ortho(0.0f, (float)size.x, (float)size.y, 0.0f, 1.0f, -1.0f);
        glm::mat4 M = glm::scale(glm::mat4(1.0f), glm::vec3(size.x, size.y, 1.0f));

        // First, we apply the fading effect.
        time += (float)deltaTime;
        menuMaterial->tint = glm::vec4(glm::smoothstep(0.00f, 2.00f, time));
        // Then we render the menu background
        menuMaterial->setup();
        menuMaterial->shader->set("transform", VP * M);
        rectangle->draw();
    }

    void onDestroy() override
    {
        // Delete all the allocated resources
        delete rectangle;
        delete menuMaterial->texture;
        delete menuMaterial->shader;
        delete menuMaterial;
        delete highlightMaterial->shader;
        delete highlightMaterial;

        if (isMusicLoaded) {
            ma_sound_uninit(&menuMusic);
            isMusicLoaded = false;
        }
        if (isAudioInitialized) {
            ma_engine_uninit(&audioEngine);
            isAudioInitialized = false;
        }
    }
};