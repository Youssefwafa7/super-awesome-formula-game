#include "wheel-spin.hpp"

namespace our {

    void WheelSpinComponent::deserialize(const nlohmann::json& data){
        if(!data.is_object()) return;

        // axis can be specified as "auto"/"x"/"y"/"z" or -1/0/1/2
        if(data.contains("axis")){
            if(data["axis"].is_string()){
                const std::string a = data["axis"].get<std::string>();
                if(a == "auto" || a == "AUTO" || a == "Auto") axis = -1;
                else if(a == "x" || a == "X") axis = 0;
                else if(a == "y" || a == "Y") axis = 1;
                else if(a == "z" || a == "Z") axis = 2;
            } else if(data["axis"].is_number_integer()){
                axis = data["axis"].get<int>();
            }
        }

        direction = data.value("direction", direction);
        debugPrintParts = data.value("debugPrintParts", debugPrintParts);
        desiredWheelCount = data.value("desiredWheelCount", desiredWheelCount);

        if(data.contains("partIndices") && data["partIndices"].is_array()){
            partIndices = data["partIndices"].get<std::vector<int>>();
        }

        if(data.contains("includeNameSubstrings") && data["includeNameSubstrings"].is_array()){
            includeNameSubstrings = data["includeNameSubstrings"].get<std::vector<std::string>>();
        }
        if(data.contains("excludeNameSubstrings") && data["excludeNameSubstrings"].is_array()){
            excludeNameSubstrings = data["excludeNameSubstrings"].get<std::vector<std::string>>();
        }

        // Clamp values
        if(axis < -1) axis = -1;
        if(axis > 2) axis = 2;
        if(desiredWheelCount < 0) desiredWheelCount = 0;
    }

}
