#ifndef CANVAS_H
#define CANVAS_H

#include "property.h"
#include <vg/vg.h>

#include <memory>

namespace lunasvg {

using GradientStop = std::pair<double, Color>;
using GradientStops = std::vector<GradientStop>;

using DashArray = std::vector<double>;

struct DashData
{
    DashArray array;
    double offset{0.0};
};

enum class TextureType
{
    Plain,
    Tiled
};

enum class BlendMode
{
    Src,
    Src_Over,
    Dst_In,
    Dst_Out
};

class CanvasImpl;

class Canvas
{
public:
    static std::shared_ptr<Canvas> create(vg::CommandListRef cl, double x, double y, double width, double height);
    static std::shared_ptr<Canvas> create(std::shared_ptr<Canvas> parent, double x, double y, double width, double height);
    static std::shared_ptr<Canvas> create(std::shared_ptr<Canvas> parent, const Rect& box);

    void setColor(const Color& color);
    void setLinearGradient(double x1, double y1, double x2, double y2, const GradientStops& stops, SpreadMethod spread, const Transform& transform);
    void setRadialGradient(double cx, double cy, double r, double fx, double fy, const GradientStops& stops, SpreadMethod spread, const Transform& transform);
    void setTexture(const Canvas* source, TextureType type, const Transform& transform);

    void fill(const Path& path, const Transform& transform, WindRule winding, BlendMode mode, double opacity);
    void stroke(const Path& path, const Transform& transform, double width, LineCap cap, LineJoin join, double miterlimit, const DashData& dash, BlendMode mode, double opacity);
    void blend(const Canvas* source, BlendMode mode, double opacity);
    void mask(const Rect& clip, const Transform& transform);

    void rgba();
    void luminance();

    unsigned int width() const;
    unsigned int height() const;
    Rect box() const;
    const std::vector<vg::CommandListHandle>& child() const { return this->child_; }

    ~Canvas();
private:
    Canvas(vg::CommandListRef cl, int x, int y, int width, int height);

    enum class PaintType : uint8_t {
        COLOR,
        LINEAR_GRADIENT,
        RADIAL_GRADIENT,
    };
    PaintType paintType;
    vg::Color color;
    float gradientParams[4];
    std::vector<vg::Color> gradientColors;
    std::vector<float> gradientStops;
    const Path* latestPath;

    vg::CommandListRef cl;
    std::vector<vg::CommandListHandle> child_;
    std::shared_ptr<Canvas> parent;
    Rect rect;
};

} // namespace lunasvg

#endif // CANVAS_H
