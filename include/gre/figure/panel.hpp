#pragma once

#include <concepts>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gre/figure/track.hpp"

namespace gre {

// A panel is one genomic view: a region and a vertical stack of tracks over
// it.  Deliberately not a layout engine -- a stack is what genomics figures
// need.
class Panel {
public:
    Panel& set_region(std::string chrom, std::int64_t start, std::int64_t end);
    Panel& set_region(GenomicRegion region);
    // Second axis for rectangular (off-diagonal) heatmaps.  Defaults to the
    // x region.
    Panel& set_region_y(GenomicRegion region);

    [[nodiscard]] const GenomicRegion& region() const noexcept { return region_; }
    [[nodiscard]] const GenomicRegion& region_y() const noexcept {
        return has_region_y_ ? region_y_ : region_;
    }

    // Takes the track by value and returns a reference to the stored copy, so
    // both `panel.add_track(SignalTrack{s}.height(80))` and post-hoc
    // configuration work.
    template <typename T>
        requires std::derived_from<T, Track>
    T& add_track(T track) {
        auto owned = std::make_unique<T>(std::move(track));
        T& reference = *owned;
        tracks_.push_back(std::move(owned));
        return reference;
    }
    Track& add_track(std::unique_ptr<Track> track);

    // ---- square (Juicebox-style) layout -----------------------------------
    //
    // Setting a matrix track switches the panel to a square arrangement:
    //
    //     [ x tracks, full map width      ]
    //     [ y ][ y ][   square contact map ]
    //
    // Tracks added with add_track() stay horizontal above the map; tracks
    // added with add_y_track() are quarter-turned and run down the map's left
    // side over the y region.  In a y track, height() sets the *column width*.
    template <typename T>
        requires std::derived_from<T, Track>
    T& set_matrix(T track) {
        auto owned = std::make_unique<T>(std::move(track));
        T& reference = *owned;
        matrix_ = std::move(owned);
        return reference;
    }
    Track& set_matrix(std::unique_ptr<Track> track);

    template <typename T>
        requires std::derived_from<T, Track>
    T& add_y_track(T track) {
        auto owned = std::make_unique<T>(std::move(track));
        T& reference = *owned;
        y_tracks_.push_back(std::move(owned));
        return reference;
    }
    Track& add_y_track(std::unique_ptr<Track> track);

    // Horizontal tracks placed *below* the map: a bottom axis, a colour bar, a
    // legend.  In a stacked panel these simply follow the main stack.
    template <typename T>
        requires std::derived_from<T, Track>
    T& add_bottom_track(T track) {
        auto owned = std::make_unique<T>(std::move(track));
        T& reference = *owned;
        bottom_tracks_.push_back(std::move(owned));
        return reference;
    }
    Track& add_bottom_track(std::unique_ptr<Track> track);

    [[nodiscard]] bool square_layout() const noexcept { return matrix_ != nullptr; }
    [[nodiscard]] Track* matrix() noexcept { return matrix_.get(); }
    [[nodiscard]] std::size_t y_track_count() const noexcept { return y_tracks_.size(); }
    [[nodiscard]] Track& y_track(std::size_t index) { return *y_tracks_.at(index); }

    // Strip under the map holding the y tracks' names.  Negative sizes it from
    // the theme's font; 0 removes it.
    Panel& set_y_label_height(double height);

    Panel& set_title(std::string title);
    // 0 lets the panel size itself from its tracks.
    Panel& set_height(double height);
    Panel& set_padding(Insets padding);
    Panel& set_label_width(double width);
    Panel& set_track_spacing(double spacing);
    Panel& set_show_grid(bool show);
    Panel& set_border(Color color, double width = 0.6);
    Panel& set_background(Color color);

    [[nodiscard]] const std::string& title() const noexcept { return title_; }
    [[nodiscard]] double height() const noexcept { return height_; }
    [[nodiscard]] std::size_t track_count() const noexcept { return tracks_.size(); }
    [[nodiscard]] Track& track(std::size_t index) { return *tracks_.at(index); }
    [[nodiscard]] const Track& track(std::size_t index) const { return *tracks_.at(index); }

private:
    friend class Figure;

    GenomicRegion region_;
    GenomicRegion region_y_;
    bool has_region_y_{false};
    std::string title_;
    double height_{0.0};
    Insets padding_{0.0};
    double label_width_{-1.0};    // negative: inherit from the theme
    double track_spacing_{-1.0};  // negative: inherit from the theme
    bool show_grid_{false};
    bool has_border_{false};
    Color border_color_{colors::transparent};
    double border_width_{0.6};
    bool has_background_{false};
    Color background_{colors::transparent};
    double y_label_height_{-1.0};
    std::vector<std::unique_ptr<Track>> tracks_;
    std::vector<std::unique_ptr<Track>> y_tracks_;
    std::vector<std::unique_ptr<Track>> bottom_tracks_;
    std::unique_ptr<Track> matrix_;
};

}  // namespace gre
