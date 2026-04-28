#include "car-rig.hpp"

#include <algorithm>

namespace our {

    void CarRigComponent::deserialize(const nlohmann::json& data){
        if(!data.is_object()) return;

        // Spin axis: "auto" / "x" / "y" / "z" or integer -1/0/1/2
        if(data.contains("spinAxis")){
            if(data["spinAxis"].is_string()){
                const std::string a = data["spinAxis"].get<std::string>();
                if(a == "auto" || a == "AUTO" || a == "Auto") spinAxis = -1;
                else if(a == "x" || a == "X") spinAxis = 0;
                else if(a == "y" || a == "Y") spinAxis = 1;
                else if(a == "z" || a == "Z") spinAxis = 2;
            } else if(data["spinAxis"].is_number_integer()){
                spinAxis = data["spinAxis"].get<int>();
            }
        }

        spinDirection = data.value("spinDirection", spinDirection);
        steerMaxAngle = data.value("steerMaxAngle", steerMaxAngle);
        debugPrint = data.value("debugPrint", debugPrint);

        // Parse wheels array
        wheels.clear();
        if(data.contains("wheels") && data["wheels"].is_array()){
            for(const auto& w : data["wheels"]){
                if(!w.is_object()) continue;

                WheelConfig cfg;
                cfg.label = w.value("label", "");
                cfg.steerNode = w.value("steerNode", "");

                if(w.contains("spinNodes") && w["spinNodes"].is_array()){
                    cfg.spinNodes = w["spinNodes"].get<std::vector<std::string>>();
                }
                if(w.contains("staticNodes") && w["staticNodes"].is_array()){
                    cfg.staticNodes = w["staticNodes"].get<std::vector<std::string>>();
                }

                wheels.push_back(std::move(cfg));
            }
        }

        // Clamp axis
        if(spinAxis < -1) spinAxis = -1;
        if(spinAxis > 2) spinAxis = 2;
    }

}
