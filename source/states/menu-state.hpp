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

#define NUM_TRACK_PRESETS 3
#define NUM_CAR_PRESETS 2

struct Button
{
    glm::vec2 position, size;
    std::function<void()> action;

    bool isInside(const glm::vec2 &v) const
    {
        return position.x <= v.x && position.y <= v.y &&
               v.x <= position.x + size.x &&
               v.y <= position.y + size.y;
    }

    glm::mat4 getLocalToWorld() const
    {
        return glm::translate(glm::mat4(1.0f), glm::vec3(position.x, position.y, 0.0f)) *
               glm::scale(glm::mat4(1.0f), glm::vec3(size.x, size.y, 1.0f));
    }
};

enum class MenuScreen
{
    MAIN_MENU,
    MODE_SELECT,
    TRACK_SELECT,
    CAR_SELECT,
    SETTINGS
};

class Menustate : public our::State
{
    our::TexturedMaterial *menuMaterial;
    our::TintedMaterial *highlightMaterial;
    our::Mesh *rectangle;
    float time;
    std::array<Button, 2> buttons;

    ma_engine audioEngine;
    ma_sound menuMusic;
    bool isAudioInitialized = false;
    bool isMusicLoaded = false;

    ImFont* titleFont  = nullptr;
    ImFont* headingFont = nullptr;
    ImFont* bodyFont   = nullptr;

    std::vector<std::string> carPresetIds;
    std::vector<std::string> carPresetLabels;
    std::vector<std::string> trackPresetIds;
    std::vector<std::string> trackPresetLabels;
    int selectedCarIndex = 0;
    int selectedTrackIndex = 0;

    MenuScreen currentScreen = MenuScreen::MAIN_MENU;

    int selectedIndex = 0; 
    bool keyboardNavActive = false; 

    GLFWgamepadstate prevGamepadState{};
    bool hasPrevGamepadState = false;
    float prevLeftX = 0.0f;
    float prevLeftY = 0.0f;

    static ImVec4 kBgColor() { return ImVec4(0.04f, 0.04f, 0.04f, 0.00f); }
    static ImVec4 kRedAccent() { return ImVec4(0.91f, 0.00f, 0.18f, 1.00f); }
    static ImVec4 kRedHover() { return ImVec4(1.00f, 0.15f, 0.30f, 1.00f); }
    static ImVec4 kRedActive() { return ImVec4(0.70f, 0.00f, 0.12f, 1.00f); }
    static ImVec4 kCardBg() { return ImVec4(0.10f, 0.10f, 0.10f, 1.00f); }
    static ImVec4 kCardSelected() { return ImVec4(0.20f, 0.04f, 0.06f, 1.00f); }
    static ImVec4 kTextWhite() { return ImVec4(1.0f, 1.0f, 1.0f, 1.0f); }
    static ImVec4 kTextDim() { return ImVec4(0.6f, 0.6f, 0.6f, 1.0f); }
    static ImVec4 kButtonDark() { return ImVec4(0.15f, 0.15f, 0.15f, 1.00f); }

    void loadPresetsFromConfig()
    {
        carPresetIds.clear();
        carPresetLabels.clear();
        trackPresetIds.clear();
        trackPresetLabels.clear();

        const auto &cfg = getApp()->getConfig();
        if (!cfg.contains("scene")) return;
        const auto &scene = cfg["scene"];
        if (!scene.contains("presets")) return;
        const auto &presets = scene["presets"];

        if (presets.contains("cars") && presets["cars"].is_array())
        {
            for (const auto &c : presets["cars"])
            {
                const std::string id = c.value("id", std::string{});
                if (id.empty()) continue;
                carPresetIds.push_back(id);
                carPresetLabels.push_back(c.value("label", id));
            }
        }

        if (presets.contains("tracks") && presets["tracks"].is_array())
        {
            for (const auto &t : presets["tracks"])
            {
                const std::string id = t.value("id", std::string{});
                if (id.empty()) continue;
                trackPresetIds.push_back(id);
                trackPresetLabels.push_back(t.value("label", id));
            }
        }

        const std::string defaultCar = (scene.contains("selection") ? scene["selection"].value("car", std::string{}) : std::string{});
        const std::string defaultTrack = (scene.contains("selection") ? scene["selection"].value("track", std::string{}) : std::string{});

        auto findIndex = [](const std::vector<std::string> &ids, const std::string &needle)
        {
            for (int i = 0; i < (int)ids.size(); i++)
                if (ids[i] == needle) return i;
            return 0;
        };

        if (!carPresetIds.empty()) selectedCarIndex = findIndex(carPresetIds, defaultCar);
        if (!trackPresetIds.empty()) selectedTrackIndex = findIndex(trackPresetIds, defaultTrack);

        if (!carPresetIds.empty()) getApp()->setSelectedCarPreset(carPresetIds[selectedCarIndex]);
        if (!trackPresetIds.empty()) getApp()->setSelectedTrackPreset(trackPresetIds[selectedTrackIndex]);
    }

    int pushF1Theme()
    {
        int colorCount = 0;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, kBgColor()); colorCount++;
        ImGui::PushStyleColor(ImGuiCol_Button, kButtonDark()); colorCount++;
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kRedHover()); colorCount++;
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kRedActive()); colorCount++;
        ImGui::PushStyleColor(ImGuiCol_Text, kTextWhite()); colorCount++;
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.2f, 0.2f, 1.0f)); colorCount++;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kCardBg()); colorCount++;

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

    static void textCentered(const char *text)
    {
        float w = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - w) * 0.5f);
        ImGui::Text("%s", text);
    }

    static void textCenteredGlow(const char *text, const ImVec4& glowColor)
    {
        float w = ImGui::CalcTextSize(text).x;
        float x = (ImGui::GetWindowWidth() - w) * 0.5f;
        ImGui::SetCursorPosX(x);

        ImVec2 screenPos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* font = ImGui::GetFont();
        float fontSize = ImGui::GetFontSize();

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

        dl->AddText(font, fontSize, screenPos, IM_COL32(255, 255, 255, 255), text);
        ImGui::Dummy(ImVec2(w, fontSize));
    }

    static bool buttonCentered(const char *label, const ImVec2 &size = ImVec2(220, 50), bool selected = false)
    {
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - size.x) * 0.5f);
        if (selected)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, kRedHover());
            ImGui::PushStyleColor(ImGuiCol_Border, kTextWhite());
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
        }
        bool clicked = ImGui::Button(label, size);
        if (selected)
        {
            ImGui::PopStyleVar(1);
            ImGui::PopStyleColor(2);
        }
        return clicked;
    }

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
            float contentH = 80.0f + 40.0f + 40.0f + 65.0f + 65.0f + 30.0f;
            ImGui::SetCursorPosY((display.y - contentH) * 0.5f);

            if(titleFont) ImGui::PushFont(titleFont);
            textCenteredGlow("SUPER AWESOME FORMULA GAME", kRedAccent());
            if(titleFont) ImGui::PopFont();
            if(bodyFont) ImGui::PushFont(bodyFont);

            ImGui::Dummy(ImVec2(0, 40));

            ImGui::PushStyleColor(ImGuiCol_Text, kTextDim());
            textCentered("Choose your mode, track, and car, then race!");
            ImGui::PopStyleColor();

            ImGui::Dummy(ImVec2(0, 40));

            if (buttonCentered("PLAY", ImVec2(260, 55), keyboardNavActive && selectedIndex == 0))
            {
                currentScreen = MenuScreen::MODE_SELECT;
                selectedIndex = 0;
            }
            ImGui::Dummy(ImVec2(0, 8));

            if (buttonCentered("SETTINGS", ImVec2(260, 55), keyboardNavActive && selectedIndex == 1))
            {
                currentScreen = MenuScreen::SETTINGS;
                selectedIndex = 0;
            }
            ImGui::Dummy(ImVec2(0, 8));

            if (buttonCentered("EXIT", ImVec2(260, 55), keyboardNavActive && selectedIndex == 2))
            {
                getApp()->close();
            }
            if(bodyFont) ImGui::PopFont();
        }
        ImGui::End();
        popF1Theme(colors);
    }

    void renderModeSelect()
    {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(display);

        int colors = pushF1Theme();
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("##ModeSelect", nullptr, flags))
        {
            float contentH = 50.0f + 30.0f + 65.0f + 65.0f + 40.0f + 55.0f;
            ImGui::SetCursorPosY((display.y - contentH) * 0.5f);

            if(headingFont) ImGui::PushFont(headingFont);
            textCenteredGlow("SELECT MODE", kRedAccent());
            if(headingFont) ImGui::PopFont();
            if(bodyFont) ImGui::PushFont(bodyFont);
            ImGui::Dummy(ImVec2(0, 30));

            if (buttonCentered("SINGLE PLAYER", ImVec2(300, 55), keyboardNavActive && selectedIndex == 0))
            {
                getApp()->setIsMultiplayer(false);
                currentScreen = MenuScreen::TRACK_SELECT;
                selectedIndex = 0;
            }
            ImGui::Dummy(ImVec2(0, 8));

            if (buttonCentered("MULTIPLAYER (SPLIT)", ImVec2(300, 55), keyboardNavActive && selectedIndex == 1))
            {
                getApp()->setIsMultiplayer(true);
                currentScreen = MenuScreen::TRACK_SELECT;
                selectedIndex = 0;
            }

            ImGui::Dummy(ImVec2(0, 40));
            if (buttonCentered("< BACK", ImVec2(160, 45), keyboardNavActive && selectedIndex == 2))
            {
                currentScreen = MenuScreen::MAIN_MENU;
                selectedIndex = 0;
            }
            if(bodyFont) ImGui::PopFont();
        }
        ImGui::End();
        popF1Theme(colors);
    }

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
            float contentH = 50.0f + 30.0f + 140.0f + 50.0f + 55.0f;
            ImGui::SetCursorPosY((display.y - contentH) * 0.5f);

            if(headingFont) ImGui::PushFont(headingFont);
            textCenteredGlow("SELECT TRACK", kRedAccent());
            if(headingFont) ImGui::PopFont();
            if(bodyFont) ImGui::PushFont(bodyFont);
            ImGui::Dummy(ImVec2(0, 30));

            const int trackCount = (int)trackPresetIds.size();
            const int displayCount = std::min(trackCount, NUM_TRACK_PRESETS);

            float cardW = 280.0f;
            float cardH = 120.0f;
            float gap = 40.0f;
            float totalW = displayCount * cardW + (displayCount - 1) * gap;
            float startX = (ImGui::GetWindowWidth() - totalW) * 0.5f;

            for (int i = 0; i < displayCount; i++)
            {
                ImGui::SetCursorPosX(startX + i * (cardW + gap));
                if (i > 0) ImGui::SameLine();

                bool isSelected = (selectedTrackIndex == i);
                bool isKeyboardSelected = keyboardNavActive && (selectedIndex == i);
                const char *name = trackPresetLabels[i].c_str();

                if (isSelected || isKeyboardSelected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, kCardSelected());
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kCardSelected());
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kCardSelected());
                    ImGui::PushStyleColor(ImGuiCol_Border, isKeyboardSelected ? kTextWhite() : kRedAccent());
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, kButtonDark());
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kRedHover());
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kButtonDark());
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                }

                char cardLabel[128];
                snprintf(cardLabel, sizeof(cardLabel), "\n  %s\n\n  %s\n", name, isSelected ? "[SELECTED]" : "Click to select");
                char btnId[64];
                snprintf(btnId, sizeof(btnId), "%s##track%d", cardLabel, i);

                if (ImGui::Button(btnId, ImVec2(cardW, cardH)))
                {
                    selectedTrackIndex = i;
                    if (i < trackCount) getApp()->setSelectedTrackPreset(trackPresetIds[i]);
                }

                ImGui::PopStyleVar(1);
                ImGui::PopStyleColor(4);
            }

            ImGui::Dummy(ImVec2(0, 40));

            float navW = 160.0f;
            float navGap = 20.0f;
            float navStartX = (ImGui::GetWindowWidth() - 2 * navW - navGap) * 0.5f;

            ImGui::SetCursorPosX(navStartX);
            bool backActive = (keyboardNavActive && selectedIndex == 3);
            if (backActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, kRedHover());
                ImGui::PushStyleColor(ImGuiCol_Border, kTextWhite());
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            } 

            if (ImGui::Button("< BACK", ImVec2(navW, 45))) {
                currentScreen = MenuScreen::MODE_SELECT;
                selectedIndex = 0;
            }
            if (backActive) { ImGui::PopStyleColor(2); ImGui::PopStyleVar(1); }

            ImGui::SameLine(0, navGap);

            bool nextActive = (keyboardNavActive && selectedIndex == 4);
            if (nextActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, kRedHover());
                ImGui::PushStyleColor(ImGuiCol_Border, kTextWhite());
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            }

            if (ImGui::Button("NEXT >", ImVec2(navW, 45))) {
                currentScreen = MenuScreen::CAR_SELECT;
                selectedIndex = 0;
            }
            if (nextActive) { ImGui::PopStyleColor(2); ImGui::PopStyleVar(1); }

            if(bodyFont) ImGui::PopFont();
        }
        ImGui::End();
        popF1Theme(colors);
    }

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
            float contentH = 50.0f + 30.0f + 140.0f + 50.0f + 55.0f;
            ImGui::SetCursorPosY((display.y - contentH) * 0.5f);

            if(headingFont) ImGui::PushFont(headingFont);
            textCenteredGlow("SELECT CAR", kRedAccent());
            if(headingFont) ImGui::PopFont();
            if(bodyFont) ImGui::PushFont(bodyFont);
            ImGui::Dummy(ImVec2(0, 30));

            const int carCount = (int)carPresetIds.size();
            const int displayCount = std::min(carCount, NUM_CAR_PRESETS);

            float cardW = 280.0f;
            float cardH = 120.0f;
            float gap = 40.0f;
            float totalW = displayCount * cardW + (displayCount - 1) * gap;
            float startX = (ImGui::GetWindowWidth() - totalW) * 0.5f;

            for (int i = 0; i < displayCount; i++)
            {
                ImGui::SetCursorPosX(startX + i * (cardW + gap));
                if (i > 0) ImGui::SameLine();

                bool isSelected = (selectedCarIndex == i);
                bool isKeyboardSelected = keyboardNavActive && (selectedIndex == i);
                const char *name = carPresetLabels[i].c_str();

                if (isSelected || isKeyboardSelected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, kCardSelected());
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kCardSelected());
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kCardSelected());
                    ImGui::PushStyleColor(ImGuiCol_Border, isKeyboardSelected ? kTextWhite() : kRedAccent());
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, kButtonDark());
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kRedHover());
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kButtonDark());
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
                    if (i < carCount) getApp()->setSelectedCarPreset(carPresetIds[i]);
                }

                ImGui::PopStyleVar(1);
                ImGui::PopStyleColor(4);
            }

            ImGui::Dummy(ImVec2(0, 40));

            float navW = 160.0f;
            float navGap = 20.0f;
            float navStartX = (ImGui::GetWindowWidth() - 2 * navW - navGap) * 0.5f;

            ImGui::SetCursorPosX(navStartX);
            bool backActive = (keyboardNavActive && selectedIndex == 2);
            if (backActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, kRedHover());
                ImGui::PushStyleColor(ImGuiCol_Border, kTextWhite());
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            }

            if (ImGui::Button("< BACK##car", ImVec2(navW, 45))) {
                currentScreen = MenuScreen::TRACK_SELECT;
                selectedIndex = 0;
            }
            if (backActive) { ImGui::PopStyleColor(2); ImGui::PopStyleVar(1); }

            ImGui::SameLine(0, navGap);

            bool startActive = (keyboardNavActive && selectedIndex == 3);
            if (startActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, kRedHover());
                ImGui::PushStyleColor(ImGuiCol_Border, kTextWhite());
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            }

            // --- REDIRECT TO LOADING STATE ---
            if (ImGui::Button("START RACE", ImVec2(navW, 45))) {
                getApp()->changeState("loading");
            }
            if (startActive) { ImGui::PopStyleColor(2); ImGui::PopStyleVar(1); }

            if(bodyFont) ImGui::PopFont();
        }
        ImGui::End();
        popF1Theme(colors);
    }

    void renderSettings()
    {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(display);

        int colors = pushF1Theme();
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("##Settings", nullptr, flags))
        {
            float contentH = 50.0f + 30.0f + 55.0f + 55.0f + 55.0f + 40.0f + 45.0f;
            ImGui::SetCursorPosY((display.y - contentH) * 0.5f);

            if(headingFont) ImGui::PushFont(headingFont);
            textCenteredGlow("SETTINGS", kRedAccent());
            if(headingFont) ImGui::PopFont();
            if(bodyFont) ImGui::PushFont(bodyFont);
            ImGui::Dummy(ImVec2(0, 30));

            bool isVignetteSelected = keyboardNavActive && selectedIndex == 0;
            std::string vignetteLabel = getApp()->isVignetteEnabled() ? "VIGNETTE: ON" : "VIGNETTE: OFF";
            if (buttonCentered(vignetteLabel.c_str(), ImVec2(380, 60), isVignetteSelected))
            {
                getApp()->setVignetteEnabled(!getApp()->isVignetteEnabled());
            }
            ImGui::Dummy(ImVec2(0, 8));

            bool isCASelected = keyboardNavActive && selectedIndex == 1;
            std::string caLabel = getApp()->isChromaticAberrationEnabled() ? "CHROMATIC ABERRATION: ON" : "CHROMATIC ABERRATION: OFF";
            if (buttonCentered(caLabel.c_str(), ImVec2(380, 60), isCASelected))
            {
                getApp()->setChromaticAberrationEnabled(!getApp()->isChromaticAberrationEnabled());
            }
            ImGui::Dummy(ImVec2(0, 8));

            bool isSpeedEffectsSelected = keyboardNavActive && selectedIndex == 2;
            std::string speedEffectsLabel = getApp()->isSpeedEffectsEnabled() ? "SPEED EFFECTS (FOV + BLUR): ON" : "SPEED EFFECTS (FOV + BLUR): OFF";
            if (buttonCentered(speedEffectsLabel.c_str(), ImVec2(380, 60), isSpeedEffectsSelected))
            {
                getApp()->setSpeedEffectsEnabled(!getApp()->isSpeedEffectsEnabled());
            }

            ImGui::Dummy(ImVec2(0, 40));

            bool isBackSelected = keyboardNavActive && selectedIndex == 3;
            if (buttonCentered("< BACK", ImVec2(260, 45), isBackSelected))
            {
                currentScreen = MenuScreen::MAIN_MENU;
                selectedIndex = 0;
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

        ImFontConfig cfg;
        cfg.OversampleH = 3;
        cfg.OversampleV = 2;

        const char* fontPaths[] = {
            "assets/fonts/SuperMario256.ttf",
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
            io.FontDefault = bodyFont;
        } else {
            io.Fonts->AddFontDefault();
            cfg.SizePixels = 48.0f; titleFont = io.Fonts->AddFontDefault(&cfg);
            cfg.SizePixels = 32.0f; headingFont = io.Fonts->AddFontDefault(&cfg);
            cfg.SizePixels = 20.0f; bodyFont = io.Fonts->AddFontDefault(&cfg);
        }

        io.Fonts->Build();
        ImGui_ImplOpenGL3_DestroyFontsTexture();
        ImGui_ImplOpenGL3_CreateFontsTexture();
    }

    void onInitialize() override
    {
        loadFonts();
        loadPresetsFromConfig();
        currentScreen = MenuScreen::MAIN_MENU;

        menuMaterial = new our::TexturedMaterial();
        menuMaterial->shader = new our::ShaderProgram();
        menuMaterial->shader->attach("assets/shaders/textured.vert", GL_VERTEX_SHADER);
        menuMaterial->shader->attach("assets/shaders/textured.frag", GL_FRAGMENT_SHADER);
        menuMaterial->shader->link();
        menuMaterial->texture = our::texture_utils::loadImage("assets/textures/background.png");
        menuMaterial->tint = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

        highlightMaterial = new our::TintedMaterial();
        highlightMaterial->shader = new our::ShaderProgram();
        highlightMaterial->shader->attach("assets/shaders/tinted.vert", GL_VERTEX_SHADER);
        highlightMaterial->shader->attach("assets/shaders/tinted.frag", GL_FRAGMENT_SHADER);
        highlightMaterial->shader->link();
        highlightMaterial->tint = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        highlightMaterial->pipelineState.blending.enabled = true;
        highlightMaterial->pipelineState.blending.equation = GL_FUNC_SUBTRACT;
        highlightMaterial->pipelineState.blending.sourceFactor = GL_ONE;
        highlightMaterial->pipelineState.blending.destinationFactor = GL_ONE;

        rectangle = new our::Mesh({
            {{0.0f, 0.0f, 0.0f}, {255, 255, 255, 255}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
            {{1.0f, 0.0f, 0.0f}, {255, 255, 255, 255}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
            {{1.0f, 1.0f, 0.0f}, {255, 255, 255, 255}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
            {{0.0f, 1.0f, 0.0f}, {255, 255, 255, 255}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        }, {0, 1, 2, 2, 3, 0});

        time = 0;

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
        ImGui::GetIO().IniFilename = nullptr;
        switch (currentScreen)
        {
        case MenuScreen::MAIN_MENU: renderMainMenu(); break;
        case MenuScreen::MODE_SELECT: renderModeSelect(); break;
        case MenuScreen::TRACK_SELECT: renderTrackSelect(); break;
        case MenuScreen::CAR_SELECT: renderCarSelect(); break;
        case MenuScreen::SETTINGS: renderSettings(); break;
        }
    }

    void onDraw(double deltaTime) override
    {
        auto &keyboard = getApp()->getKeyboard();
        int itemCount = 0;
        int presetsCount = (currentScreen == MenuScreen::TRACK_SELECT) ? NUM_TRACK_PRESETS : NUM_CAR_PRESETS;

        bool padUp = false;
        bool padDown = false;
        bool padLeft = false;
        bool padRight = false;
        bool padConfirm = false;
        bool padBack = false;

        GLFWgamepadstate padState;
        if(glfwGetGamepadState(GLFW_JOYSTICK_1, &padState)){
            const bool havePrev = hasPrevGamepadState;
            if(!hasPrevGamepadState){
                prevGamepadState = padState;
                prevLeftX = padState.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
                prevLeftY = padState.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
                hasPrevGamepadState = true;
            }

            auto justPressed = [&](int button){
                return padState.buttons[button] == GLFW_PRESS && (!havePrev || prevGamepadState.buttons[button] == GLFW_RELEASE);
            };

            padUp = justPressed(GLFW_GAMEPAD_BUTTON_DPAD_UP);
            padDown = justPressed(GLFW_GAMEPAD_BUTTON_DPAD_DOWN);
            padLeft = justPressed(GLFW_GAMEPAD_BUTTON_DPAD_LEFT);
            padRight = justPressed(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT);
            padConfirm = justPressed(GLFW_GAMEPAD_BUTTON_A);
            padBack = justPressed(GLFW_GAMEPAD_BUTTON_B);

            const float axisDeadzone = 0.5f;
            const float lx = padState.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
            const float ly = padState.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];

            if(lx < -axisDeadzone && (!havePrev || prevLeftX >= -axisDeadzone)) padLeft = true;
            if(lx > axisDeadzone && (!havePrev || prevLeftX <= axisDeadzone)) padRight = true;
            if(ly < -axisDeadzone && (!havePrev || prevLeftY >= -axisDeadzone)) padUp = true;
            if(ly > axisDeadzone && (!havePrev || prevLeftY <= axisDeadzone)) padDown = true;

            prevGamepadState = padState;
            prevLeftX = lx;
            prevLeftY = ly;
        } else {
            hasPrevGamepadState = false;
            prevLeftX = 0.0f;
            prevLeftY = 0.0f;
        }

        switch (currentScreen)
        {
        case MenuScreen::MAIN_MENU: itemCount = 3; break;
        case MenuScreen::MODE_SELECT: itemCount = 3; break;
        case MenuScreen::TRACK_SELECT: itemCount = 2 + NUM_TRACK_PRESETS; break;
        case MenuScreen::CAR_SELECT: itemCount = 2 + NUM_CAR_PRESETS; break;
        case MenuScreen::SETTINGS: itemCount = 4; break;
        }

        bool isSpecialScreen = (currentScreen == MenuScreen::TRACK_SELECT || currentScreen == MenuScreen::CAR_SELECT);

        if (keyboard.justPressed(GLFW_KEY_DOWN) || keyboard.justPressed(GLFW_KEY_UP) || padDown || padUp) {
            bool down = keyboard.justPressed(GLFW_KEY_DOWN) || padDown;
            keyboardNavActive = true;
            if (currentScreen == MenuScreen::MAIN_MENU || currentScreen == MenuScreen::MODE_SELECT || currentScreen == MenuScreen::SETTINGS) {
                if ((down && selectedIndex < itemCount - 1) || (!down && selectedIndex > 0))
                    selectedIndex += down ? 1 : -1;
            } else if (isSpecialScreen) {
                if (down && selectedIndex < presetsCount) selectedIndex = presetsCount;
                else if (!down && selectedIndex >= presetsCount) selectedIndex = 0;
            }
        }

        if (isSpecialScreen && (keyboard.justPressed(GLFW_KEY_RIGHT) || keyboard.justPressed(GLFW_KEY_LEFT) || padRight || padLeft)) {
            bool right = keyboard.justPressed(GLFW_KEY_RIGHT) || padRight;
            int rowStart = (selectedIndex < presetsCount) ? 0 : presetsCount;
            int rowEnd   = (selectedIndex < presetsCount) ? presetsCount : itemCount;
            int rowSize  = rowEnd - rowStart;
            keyboardNavActive = true;
            int localIdx = selectedIndex - rowStart;
            if (right) localIdx = (localIdx + 1) % rowSize;
            else localIdx = (localIdx - 1 + rowSize) % rowSize;
            selectedIndex = rowStart + localIdx;
        }

        if (keyboard.justPressed(GLFW_KEY_ENTER) || keyboard.justPressed(GLFW_KEY_SPACE) || padConfirm)
        {
            keyboardNavActive = true;
            switch (currentScreen)
            {
            case MenuScreen::MAIN_MENU:
                if (selectedIndex == 0) { currentScreen = MenuScreen::MODE_SELECT; selectedIndex = 0; }
                else if (selectedIndex == 1) { currentScreen = MenuScreen::SETTINGS; selectedIndex = 0; }
                else if (selectedIndex == 2) getApp()->close();
                break;
            case MenuScreen::MODE_SELECT:
                if (selectedIndex == 0) { getApp()->setIsMultiplayer(false); currentScreen = MenuScreen::TRACK_SELECT; selectedIndex = 0; }
                else if (selectedIndex == 1) { getApp()->setIsMultiplayer(true); currentScreen = MenuScreen::TRACK_SELECT; selectedIndex = 0; }
                else if (selectedIndex == 2) { currentScreen = MenuScreen::MAIN_MENU; selectedIndex = 0; }
                break;
            case MenuScreen::TRACK_SELECT:
                if (selectedIndex < (int)trackPresetIds.size()) {
                    selectedTrackIndex = selectedIndex;
                    getApp()->setSelectedTrackPreset(trackPresetIds[selectedIndex]);
                } else if (selectedIndex == 3) { currentScreen = MenuScreen::MODE_SELECT; selectedIndex = 0; }
                else if (selectedIndex == 4) { currentScreen = MenuScreen::CAR_SELECT; selectedIndex = 0; }
                break;
            case MenuScreen::CAR_SELECT:
                if (selectedIndex < (int)carPresetIds.size()) {
                    selectedCarIndex = selectedIndex;
                    getApp()->setSelectedCarPreset(carPresetIds[selectedIndex]);
                } else if (selectedIndex == 2) { currentScreen = MenuScreen::TRACK_SELECT; selectedIndex = 0; }
                else if (selectedIndex == 3) { 
                    // --- REDIRECT TO LOADING STATE ---
                    getApp()->changeState("loading"); 
                }
                break;
            case MenuScreen::SETTINGS:
                if (selectedIndex == 0) getApp()->setVignetteEnabled(!getApp()->isVignetteEnabled());
                else if (selectedIndex == 1) getApp()->setChromaticAberrationEnabled(!getApp()->isChromaticAberrationEnabled());
                else if (selectedIndex == 2) getApp()->setSpeedEffectsEnabled(!getApp()->isSpeedEffectsEnabled());
                else if (selectedIndex == 3) { currentScreen = MenuScreen::MAIN_MENU; selectedIndex = 0; }
                break;
            }
        }

        if (keyboard.justPressed(GLFW_KEY_ESCAPE) || padBack)
        {
            keyboardNavActive = true;
            if (currentScreen == MenuScreen::CAR_SELECT) currentScreen = MenuScreen::TRACK_SELECT;
            else if (currentScreen == MenuScreen::TRACK_SELECT) currentScreen = MenuScreen::MODE_SELECT;
            else if (currentScreen == MenuScreen::MODE_SELECT || currentScreen == MenuScreen::SETTINGS) currentScreen = MenuScreen::MAIN_MENU;
            else getApp()->close();
        }

        glm::ivec2 fSize = getApp()->getFrameBufferSize();
        glViewport(0, 0, fSize.x, fSize.y);
        glm::mat4 VP = glm::ortho(0.0f, (float)fSize.x, (float)fSize.y, 0.0f, 1.0f, -1.0f);
        glm::mat4 M = glm::scale(glm::mat4(1.0f), glm::vec3(fSize.x, fSize.y, 1.0f));

        time += (float)deltaTime;
        menuMaterial->tint = glm::vec4(glm::smoothstep(0.00f, 2.00f, time));
        menuMaterial->setup();
        menuMaterial->shader->set("transform", VP * M);
        rectangle->draw();
    }

    void onDestroy() override
    {
        delete rectangle;
        delete menuMaterial->texture;
        delete menuMaterial->shader;
        delete menuMaterial;
        delete highlightMaterial->shader;
        delete highlightMaterial;
        if (isMusicLoaded) ma_sound_uninit(&menuMusic);
        if (isAudioInitialized) ma_engine_uninit(&audioEngine);
    }
};