#include "gre/figure/panel.hpp"

#include "gre/core/error.hpp"

namespace gre {

Panel& Panel::set_region(std::string chrom, std::int64_t start, std::int64_t end) {
    return set_region(GenomicRegion{std::move(chrom), start, end});
}

Panel& Panel::set_region(GenomicRegion region) {
    if (region.chrom.empty()) {
        throw Error(ErrorCode::invalid_argument, "panel region needs a chromosome name");
    }
    if (region.end <= region.start) {
        throw Error(ErrorCode::invalid_argument,
                    "panel region must be non-empty: " + region.chrom + ":" +
                        std::to_string(region.start) + "-" + std::to_string(region.end));
    }
    region_ = std::move(region);
    return *this;
}

Panel& Panel::set_region_y(GenomicRegion region) {
    if (region.end <= region.start) {
        throw Error(ErrorCode::invalid_argument, "panel y region must be non-empty");
    }
    region_y_ = std::move(region);
    has_region_y_ = true;
    return *this;
}

Track& Panel::add_track(std::unique_ptr<Track> track) {
    if (track == nullptr) throw Error(ErrorCode::invalid_argument, "null track");
    Track& reference = *track;
    tracks_.push_back(std::move(track));
    return reference;
}

Track& Panel::set_matrix(std::unique_ptr<Track> track) {
    if (track == nullptr) throw Error(ErrorCode::invalid_argument, "null matrix track");
    matrix_ = std::move(track);
    return *matrix_;
}

Track& Panel::add_y_track(std::unique_ptr<Track> track) {
    if (track == nullptr) throw Error(ErrorCode::invalid_argument, "null track");
    Track& reference = *track;
    y_tracks_.push_back(std::move(track));
    return reference;
}

Track& Panel::add_bottom_track(std::unique_ptr<Track> track) {
    if (track == nullptr) throw Error(ErrorCode::invalid_argument, "null track");
    Track& reference = *track;
    bottom_tracks_.push_back(std::move(track));
    return reference;
}

Panel& Panel::set_y_label_height(double height) {
    y_label_height_ = height;
    return *this;
}

Panel& Panel::set_title(std::string title) {
    title_ = std::move(title);
    return *this;
}

Panel& Panel::set_height(double height) {
    height_ = height;
    return *this;
}

Panel& Panel::set_padding(Insets padding) {
    padding_ = padding;
    return *this;
}

Panel& Panel::set_label_width(double width) {
    label_width_ = width;
    return *this;
}

Panel& Panel::set_track_spacing(double spacing) {
    track_spacing_ = spacing;
    return *this;
}

Panel& Panel::set_show_grid(bool show) {
    show_grid_ = show;
    return *this;
}

Panel& Panel::set_border(Color color, double width) {
    border_color_ = color;
    border_width_ = width;
    has_border_ = true;
    return *this;
}

Panel& Panel::set_background(Color color) {
    background_ = color;
    has_background_ = true;
    return *this;
}

}  // namespace gre
