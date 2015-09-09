#pragma once

#include "platform.h"
#include "scene/styleParam.h"

#include <string>
#include <vector>

namespace Tangram {

using Function = std::string;

class StyleContext;

struct DrawRule {

    std::string style;
    std::vector<StyleParam> parameters;

    DrawRule(const std::string& _style, const std::vector<StyleParam>& _parameters, bool _sorted = false);

    DrawRule merge(DrawRule& _other) const;
    std::string toString() const;

    void eval(const StyleContext& _ctx);

    const StyleParam& findParameter(StyleParamKey _key) const;

    template<typename T>
    bool get(StyleParamKey _key, T& _value) const {
        auto& param = findParameter(_key);
        if (!param) { return false; }
        if (!param.value.is<T>()) {
            logMsg("Error: wrong type '%d'for StyleParam '%d' \n",
                   param.value.which(), _key);
            return false;
        }
        _value = param.value.get<T>();
        return true;
    }

    bool operator<(const DrawRule& _rhs) const;
    int compare(const DrawRule& _rhs) const { return style.compare(_rhs.style); }

};

}
