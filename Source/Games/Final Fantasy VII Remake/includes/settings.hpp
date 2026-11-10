// Loosely based on renodx settings system https://github.com/clshortfuse/renodx/blob/main/src/utils/settings.hpp
#include <functional>
#include <string>
#include <vector>
#include <imgui.h>
#include "../../../Core/utils/config.hpp"

#define ICON_FK_UNDO "U"

namespace Luma {
namespace Settings
{
    extern "C" const char* NAME;

    enum class SettingValueType
    { 
        BOOLEAN,
        INTEGER,
        FLOAT
    };

    struct Setting
    {
        std::string key;
        float* binding = nullptr;
        SettingValueType type = SettingValueType::FLOAT;
        float default_value = 0.f;
        bool can_reset = true;
        std::string label = key;
        std::string tooltip;
        std::vector<std::string> labels;
        float min = 0.f;
        float max = 100.f;
        std::string format = "%.0f";

        std::function<bool()> is_enabled = []() { return true; };
        std::function<bool()> is_visible = []() { return true; };
        std::function<float(float value)> parse = [](float value) { return value; };

        float value = default_value;
        int value_as_int = static_cast<int>(default_value);

        float GetMax() const { 
            switch (this->type) {
                case SettingValueType::BOOLEAN: return 1.f;
                case SettingValueType::INTEGER: return this->labels.empty() ? this->max : this->labels.size() - 1;
                case SettingValueType::FLOAT: return this->max;
            }
        }

        float GetValue() const { 
            switch (this->type) {
                case SettingValueType::BOOLEAN: return static_cast<float>(this->value_as_int);
                case SettingValueType::INTEGER: return static_cast<float>(this->value_as_int);
                case SettingValueType::FLOAT: 
                default:
                    return this->value;
            }
        }

        Setting* Set(float value) {
            this->value = value;
            this->value_as_int = static_cast<int>(value);
            return this;
        }

        Setting* Write() {
            if (this->binding != nullptr) {
                *this->binding = this->parse(this->GetValue());
            }
            return this;
        }

        Setting* Save() {
            ::Config::Set(NAME, this->key.c_str(), this->GetValue());
            return this;
        }

        Setting* Load() {
            float v;
            if(::Config::Get(NAME, this->key.c_str(), v)) {
                this->Set(v)->Write();
            } else {
                this->Set(this->default_value)->Write();
            }
            return this;
        }

        void Draw() {
            if (!this->is_visible()) return;

            ImGui::PushID(this->key.c_str());
            ImGui::BeginDisabled(!this->is_enabled());

            switch (this->type) {
                case SettingValueType::BOOLEAN: {
                    int v = this->GetValue() != 0.f;
                    if (ImGui::SliderInt(this->label.c_str(), reinterpret_cast<int*>(&v), 0, 1, v ? "On" : "Off")) {
                        this->Set(v ? 1.f : 0.f)->Write()->Save();
                    }
                    break;
                }
                case SettingValueType::INTEGER: {
                    std::string format = this->labels.empty() ? this->format : this->labels[this->value_as_int];
                    int v = static_cast<int>(this->GetValue());
                    if (ImGui::SliderInt(this->label.c_str(), &v, static_cast<int>(this->min), static_cast<int>(this->max), format.c_str())) {
                        this->Set(static_cast<float>(v))->Write()->Save();
                    }
                    break;
                }
                case SettingValueType::FLOAT: {
                    float v = this->GetValue();
                    if (ImGui::SliderFloat(this->label.c_str(), &v, this->min, this->max, this->format.c_str())) {
                        this->Set(v)->Write()->Save();
                    }
                    break;
                }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !this->tooltip.empty()) {
                ImGui::SetTooltip("%s", this->tooltip.c_str());
            }
            ImGui::SameLine();
            if (this->can_reset && this->value != this->default_value) {
                if (ImGui::SmallButton(ICON_FK_UNDO)) {
                    this->Set(this->default_value)->Write();
                }
            } else {
                const auto& style = ImGui::GetStyle();
                ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                size.x += style.FramePadding.x;
                size.y += style.FramePadding.y;
                ImGui::InvisibleButton("", ImVec2(size.x, size.y));
            }

            ImGui::EndDisabled();
            ImGui::PopID();
        }
    };

    struct Section
    {
        std::string label;
        std::function<bool()> is_visible = []() { return true; };
        std::vector<Setting*> settings;

        void Draw() {
            if (!this->is_visible()) return;

            if (!this->label.empty()){
                if (ImGui::TreeNodeEx(this->label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    for (auto& setting : this->settings) {
                        setting->Draw();
                    }
                    ImGui::TreePop();
                }
            } else {
                for (auto& setting : this->settings) {
                    setting->Draw();
                }
            }
        }

        void Load() {
            for (auto& setting : this->settings) {
                setting->Load();
            }
        }
    };

    using Settings = std::vector<Section*>;
    static Settings* settings = nullptr;

    static void LoadSettings() {
        if (settings == nullptr) return;
        for (auto* section : *settings) {
            section->Load();
        }
	}

    void DrawSettings() {
        for (auto* section : *settings) {
            section->Draw();
        }
	}

    void Initialize(Settings* s) {
        settings = s;
    }
}
}