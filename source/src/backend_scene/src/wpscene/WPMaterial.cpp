#include "WPMaterial.h"

using namespace wallpaper::wpscene;

bool WPMaterialPassBindItem::FromJson(const nlohmann::json& json) {
    GET_JSON_NAME_VALUE(json, "name", name);
    GET_JSON_NAME_VALUE(json, "index", index);
    return true;
}


namespace
{
using Textures  = std::vector<std::string>;
using Combos    = std::unordered_map<std::string, int32_t>;
using Constants = std::unordered_map<std::string, std::vector<float>>;

// what a pass and a material have in common: lay one's values over the other's
void Merge(Textures& textures, Constants& constants, Combos& combos, const WPMaterialPass& p) {
    if (p.textures.size() > textures.size()) textures.resize(p.textures.size());
    for (std::size_t i = 0; i < p.textures.size(); i++) {
        if (! p.textures[i].empty()) textures[i] = p.textures[i];
    }
    for (const auto& el : p.constantshadervalues) constants[el.first] = el.second;
    for (const auto& el : p.combos) combos[el.first] = el.second;
}

// and read them from json
void ReadContent(const nlohmann::json& json, Textures& textures, Constants& constants,
                 Combos& combos) {
    if (json.contains("textures")) {
        for (const auto& jT : json.at("textures")) {
            std::string tex;
            if (! jT.is_null()) GET_JSON_VALUE(jT, tex);
            textures.push_back(tex);
        }
    }
    if (json.contains("constantshadervalues")) {
        for (const auto& jC : json.at("constantshadervalues").items()) {
            std::string        name;
            std::vector<float> value;
            GET_JSON_VALUE(jC.key(), name);
            GET_JSON_VALUE(jC.value(), value);
            constants[name] = value;
        }
    }
    if (json.contains("combos")) {
        for (const auto& jC : json.at("combos").items()) {
            std::string name;
            int32_t     value { 0 };
            GET_JSON_VALUE(jC.key(), name);
            GET_JSON_VALUE(jC.value(), value);
            combos[name] = value;
        }
    }
}
} // namespace

void WPMaterialPass::Update(const WPMaterialPass& p) {
    Merge(textures, constantshadervalues, combos, p);
}

void WPMaterial::MergePass(const WPMaterialPass& p) {
    Merge(textures, constantshadervalues, combos, p);
}

bool WPMaterialPass::FromJson(const nlohmann::json& json) {
    ReadContent(json, textures, constantshadervalues, combos);
    GET_JSON_NAME_VALUE_NOWARN(json, "target", target);
    if(json.contains("bind")) {
        for(const auto& jB:json.at("bind")) {
            WPMaterialPassBindItem bindItem;
            bindItem.FromJson(jB);
            bind.push_back(bindItem);
        }
    }
    return true;
}

bool WPMaterial::FromJson(const nlohmann::json& json) {
    if(!json.contains("passes") || !json.at("passes").is_array() || json.at("passes").size() == 0) {
        LOG_ERROR("material no data");
        return false;
    }
    const auto jContent = json.at("passes").at(0);
    if(!jContent.contains("shader")) {
        LOG_ERROR("material no shader");
        return false;
    }
	GET_JSON_NAME_VALUE(jContent, "blending", blending);
	GET_JSON_NAME_VALUE(jContent, "cullmode", cullmode);
	GET_JSON_NAME_VALUE(jContent, "depthtest", depthtest);
	GET_JSON_NAME_VALUE(jContent, "depthwrite", depthwrite);
	GET_JSON_NAME_VALUE(jContent, "shader", shader);
    ReadContent(jContent, textures, constantshadervalues, combos);
    return true;
}