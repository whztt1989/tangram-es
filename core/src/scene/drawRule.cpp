#include "drawRule.h"

#include "tile/tileBuilder.h"
#include "scene/scene.h"
#include "scene/sceneLayer.h"
#include "scene/stops.h"
#include "scene/styleContext.h"
#include "style/style.h"
#include "platform.h"
#include "drawRuleWarnings.h"
#include "util/hash.h"
#include "log.h"

#include <algorithm>

namespace Tangram {

DrawRuleData::DrawRuleData(std::string _name, int _id,
                           std::vector<StyleParam> _parameters)
    : parameters(std::move(_parameters)),
      name(std::move(_name)),
      id(_id) {}

std::string DrawRuleData::toString() const {
    std::string str = "{\n";
    for (auto& p : parameters) {
         str += " { "
             + std::to_string(static_cast<int>(p.key))
             + ", "
             + p.toString()
             + " }\n";
    }
    str += "}\n";

    return str;
}

DrawRule::DrawRule(const DrawRuleData& _ruleData, const std::string& _layerName, size_t _layerDepth) :
    name(&_ruleData.name),
    id(_ruleData.id) {

    for (const auto& param : _ruleData.parameters) {
        auto key = static_cast<uint8_t>(param.key);
        active[key] = true;
        params[key] = { &param, _layerName.c_str(), _layerDepth };
    }
}

void DrawRule::merge(const DrawRuleData& _ruleData, const SceneLayer& _layer) {

    evalConflict(*this, _ruleData, _layer);

    const auto depthNew = _layer.depth();
    const char* layerNew = _layer.name().c_str();

    for (const auto& paramNew : _ruleData.parameters) {

        auto key = static_cast<uint8_t>(paramNew.key);
        auto& param = params[key];

        if (!active[key] || depthNew > param.depth ||
            (depthNew == param.depth && strcmp(layerNew, param.name) > 0)) {
            param = { &paramNew, layerNew, depthNew };
            active[key] = true;
        }
    }
}

bool DrawRule::contains(StyleParamKey _key) const {
    return bool(findParameter(_key));
}

const StyleParam& DrawRule::findParameter(StyleParamKey _key) const {
    static const StyleParam NONE;

    uint8_t key = static_cast<uint8_t>(_key);
    if (!active[key]) { return NONE; }
    return *params[key].param;
}

const std::string& DrawRule::getStyleName() const {

    const auto& style = findParameter(StyleParamKey::style);

    if (style) {
        return style.value.get<std::string>();
    } else {
        return *name;
    }
}

const char* DrawRule::getLayerName(StyleParamKey _key) const {
    return params[static_cast<uint8_t>(_key)].name;
}

size_t DrawRule::getParamSetHash() const {
    size_t seed = 0;
    for (size_t i = 0; i < StyleParamKeySize; i++) {
        if (active[i]) { hash_combine(seed, params[i].name); }
    }
    return seed;
}

void DrawRule::logGetError(StyleParamKey _expectedKey, const StyleParam& _param) const {
    LOGE("wrong type '%d'for StyleParam '%d'", _param.value.which(), _expectedKey);
}

bool DrawRuleMergeSet::match(const Feature& _feature, const SceneLayer& _layer, StyleContext& _ctx) {

    _ctx.setFeature(_feature);
    m_matchedRules.clear();
    m_queuedLayers.clear();

    // If uber layer is marked not visible return immediately
    if (!_layer.visible()) {
        return false;
    }

    // If the first filter doesn't match, return immediately
    if (!_layer.filter().eval(_feature, _ctx)) { return false; }

    m_queuedLayers.push_back(&_layer);

    // Iterate depth-first over the layer hierarchy
    while (!m_queuedLayers.empty()) {

        // Pop a layer off the top of the stack
        const auto& layer = *m_queuedLayers.back();
        m_queuedLayers.pop_back();

        // Merge rules from layer into accumulated set
        mergeRules(layer);

        // Push each of the layer's matching sublayers onto the stack
        for (const auto& sublayer : layer.sublayers()) {
            // Skip matching this sublayer if marked not visible
            if (!sublayer.visible()) {
                continue;
            }

            if (sublayer.filter().eval(_feature, _ctx)) {
                m_queuedLayers.push_back(&sublayer);
            }
        }
    }

    return true;
}

void DrawRuleMergeSet::apply(const Feature& _feature, const SceneLayer& _layer,
                             StyleContext& _ctx, TileBuilder& _builder) {

    // If no rules matched the feature, return immediately
    if (!match(_feature, _layer, _ctx)) { return; }

    // For each matched rule, find the style to be used and
    // build the feature with the rule's parameters
    for (auto& rule : m_matchedRules) {

        StyleBuilder* style = _builder.getStyleBuilder(rule.getStyleName());

        if (!style) {
            LOGN("Invalid style %s", rule.getStyleName().c_str());
            continue;
        }

        bool valid = evaluateRuleForContext(rule, _ctx);

        if (valid) {

            // build outline explicitly with outline style
            const auto& outlineStyleName = rule.findParameter(StyleParamKey::outline_style);
            if (outlineStyleName) {
                auto& styleName = outlineStyleName.value.get<std::string>();
                auto* outlineStyle = _builder.getStyleBuilder(styleName);
                if (!outlineStyle) {
                    LOGN("Invalid style %s", styleName.c_str());
                } else {
                    rule.isOutlineOnly = true;
                    outlineStyle->addFeature(_feature, rule);
                    rule.isOutlineOnly = false;
                }
            }

            // build feature with style
            style->addFeature(_feature, rule);
        }
    }
}

bool DrawRuleMergeSet::evaluateRuleForContext(DrawRule& rule, StyleContext& ctx) {

        bool visible;
        if (rule.get(StyleParamKey::visible, visible) && !visible) {
            return false;
        }

        bool valid = true;
        for (size_t i = 0; i < StyleParamKeySize; ++i) {

            if (!rule.active[i]) {
                rule.params[i].param = nullptr;
                continue;
            }

            auto*& param = rule.params[i].param;

            if (param->value.is<StyleParam::Function>()) {

                m_evaluated[i].key = param->key;

                if (!ctx.evalStyle(param->value.get<StyleParam::Function>().id,
                                   param->key, m_evaluated[i].value)) {

                    if (StyleParam::isRequired(param->key)) {
                        valid = false;
                        break;
                    } else {
                        rule.active[i] = false;
                    }
                }
                // Set 'param' pointer to evaluated StyleParam
                param = &m_evaluated[i];
            }
            else if (param->value.is<Stops>()) {
                auto& stops = param->value.get<Stops>();
                float zoom = ctx.getKeywordZoom();

                m_evaluated[i].key = param->key;
                param = &m_evaluated[i];

                if (StyleParam::isColor(param->key)) {
                    m_evaluated[i].value = stops.evalColor(zoom);
                } else if (StyleParam::isWidth(param->key)) {
                    m_evaluated[i].value = StyleParam::Width {
                        stops.evalWidth(zoom),
                        stops.evalWidth(zoom + 1)
                    };
                } else if (StyleParam::isOffsets(param->key)) {
                    m_evaluated[i].value = stops.evalVec2(zoom);
                } else {
                    m_evaluated[i].value = stops.evalFloat(zoom);
                }
            }

        }

        return valid;
}

void DrawRuleMergeSet::mergeRules(const SceneLayer& _layer) {

    size_t pos, end = m_matchedRules.size();

    for (const auto& rule : _layer.rules()) {
        for (pos = 0; pos < end; pos++) {
            if (m_matchedRules[pos].id == rule.id) { break; }
        }

        if (pos == end) {
            m_matchedRules.emplace_back(rule, _layer.name(), _layer.depth());
        } else {
            m_matchedRules[pos].merge(rule, _layer);
        }
    }
}

}
