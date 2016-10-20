#pragma once

#include "data/tileSource.h"
#include "scene/styleContext.h"
#include "scene/drawRule.h"
#include "labels/labelCollider.h"

namespace Tangram {

class DataLayer;
class TileSource;
class Tile;
struct TileData;
class StyleBuilder;

class TileBuilder {

public:

    TileBuilder(std::shared_ptr<Scene> _scene);

    ~TileBuilder();

    StyleBuilder* getStyleBuilder(const std::string& _name);

    std::shared_ptr<Tile> build(TileID _tileID, const TileData& _data, const TileSource& _source);

    const Scene& scene() const { return *m_scene; }

private:
    std::shared_ptr<Scene> m_scene;

    StyleContext m_styleContext;
    DrawRuleMergeSet m_ruleSet;

    LabelCollider m_labelLayout;

    fastmap<std::string, std::unique_ptr<StyleBuilder>> m_styleBuilder;
};

}
