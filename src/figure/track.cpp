#include "gre/figure/track.hpp"

namespace gre {

const Theme& Track::theme() const noexcept {
    // A track drawn without a preceding prepare() still needs sane colours.
    static const Theme fallback = Theme::light();
    return view_.theme != nullptr ? *view_.theme : fallback;
}

}  // namespace gre
