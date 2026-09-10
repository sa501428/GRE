#pragma once

#include <string>
#include <vector>

#include "gre/data/feature_source.hpp"
#include "gre/figure/track.hpp"

namespace gre {

// Gene models: an intron line with strand arrows, exon boxes, thicker coding
// boxes, and names.  Overlapping genes are packed onto as many rows as they
// need, which is what sets the track's height.
class GeneTrack : public TrackBase<GeneTrack> {
public:
    explicit GeneTrack(FeatureSourcePtr source);
    explicit GeneTrack(std::vector<Feature> features);

    GeneTrack& color(Color value);
    GeneTrack& row_height(double value);
    GeneTrack& exon_height(double value);
    // Height of non-coding (UTR) exon segments.
    GeneTrack& utr_height(double value);
    GeneTrack& max_rows(int value);
    // Draw everything on a single row, ignoring overlap.
    GeneTrack& collapsed(bool value);
    GeneTrack& show_labels(bool value);
    GeneTrack& show_arrows(bool value);
    GeneTrack& label_font_size(double value);

    void prepare(const ViewContext& context) override;
    void draw(Canvas& canvas, const TrackRect& rect) const override;

    [[nodiscard]] const std::vector<Feature>& features() const noexcept { return features_; }
    [[nodiscard]] int rows() const noexcept { return rows_; }

protected:
    [[nodiscard]] double default_height() const override;

private:
    struct Placed {
        std::size_t index{};
        int row{};
        // Where the label goes; empty when it did not fit.
        bool label_after{true};
        bool label_visible{false};
    };

    FeatureSourcePtr source_;
    std::vector<Feature> features_;
    bool have_data_{false};

    Color color_{colors::transparent};
    double row_height_{13.0};
    double exon_height_{9.0};
    double utr_height_{5.0};
    int max_rows_{12};
    bool collapsed_{false};
    bool show_labels_{true};
    bool show_arrows_{true};
    double label_font_size_{0.0};

    std::vector<Placed> placed_;
    int rows_{1};
};

}  // namespace gre
