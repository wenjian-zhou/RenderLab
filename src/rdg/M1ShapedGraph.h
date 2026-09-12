#pragma once

#include "GraphBuilder.h"

namespace renderlab::rdg
{
    // The frozen M1 manual pipeline as declarations only (docs/m1-reference.md §3):
    // six textures, three Raster passes, imported+exported back buffer. Shared by
    // S4.6 goldens, --dump-rdg, and the S4.1 declarations test so they cannot drift.
    void BuildM1ShapedGraph(GraphBuilder& builder);
}
