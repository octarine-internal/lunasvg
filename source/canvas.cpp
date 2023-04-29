#include "canvas.h"

#include <cmath>
#include <vg_util.h>

namespace lunasvg {

std::shared_ptr<Canvas> Canvas::create(vg::CommandListRef cl, double x, double y, double width, double height)
{
    if (width <= 0.0 || height <= 0.0)
        return std::shared_ptr<Canvas>(new Canvas(cl, 0, 0, 1, 1));

    auto l = static_cast<int>(floor(x));
    auto t = static_cast<int>(floor(y));
    auto r = static_cast<int>(ceil(x + width));
    auto b = static_cast<int>(ceil(y + height));
    return std::shared_ptr<Canvas>(new Canvas(cl, l, t, r - l, b - t));
}

std::shared_ptr<Canvas> Canvas::create(std::shared_ptr<Canvas> parent, double x, double y, double width, double height)
{
    auto handle = vg::createCommandList(parent->cl.m_Context, vg::CommandListFlags::Cacheable);
    auto res = create(vg::makeCommandListRef(parent->cl.m_Context, handle), x, y, width, height);
    res->parent = parent;
    if (vg::isValid(handle) && parent != nullptr) {
        std::shared_ptr<Canvas> orig_parent = parent;
        while (orig_parent->parent != nullptr)
            orig_parent = orig_parent->parent;
        orig_parent->child_.emplace_back(handle);
    }
    return res;
}

std::shared_ptr<Canvas> Canvas::create(std::shared_ptr<Canvas> parent, const Rect& box)
{
    return create(parent, box.x, box.y, box.w, box.h);
}

Canvas::Canvas(vg::CommandListRef cl, int x, int y, int width, int height)
{
    this->cl = cl;
    paintType = PaintType::COLOR;
    color = vg::Colors::White;
    rect = { (float)x, (float)y, (float)width, (float)height };
    latestPath = nullptr;
    vg::clSetScissor(cl, x, y, width, height);
}

Canvas::~Canvas()
{
}

void Canvas::setColor(const Color& color)
{
    this->paintType = PaintType::COLOR;
    this->color = vg::color4f(
        color.r,
        color.g,
        color.b,
        color.a
    );
}

void Canvas::setLinearGradient(double x1, double y1, double x2, double y2, const GradientStops& stops, SpreadMethod spread, const Transform& transform)
{
    this->paintType = PaintType::LINEAR_GRADIENT;
    auto dx = (float)(x2 - x1);
    auto dy = (float)(y2 - y1);
    float xform[6]{
        dy, -dx,
        dx, dy,
        (float)x1, (float)y1,
    };
    float transform_[6]{
        (float)transform.m00, (float)transform.m10, (float)transform.m01,
        (float)transform.m11, (float)transform.m02, (float)transform.m12,
    };
    float res[6];
    vgutil::multiplyMatrix3(transform_, xform, res);
    float s[2], e[2];
    vgutil::transformPos2D(0.f, 0.f, res, s);
    vgutil::transformPos2D(0.f, 1.f, res, e);
    this->gradientParams[0] = s[0];
    this->gradientParams[1] = s[1];
    this->gradientParams[2] = e[0];
    this->gradientParams[3] = e[1];
    this->gradientColors.clear();
    this->gradientStops.clear();
    for (auto& stop : stops) {
        this->gradientStops.emplace_back(stop.first);
        auto& color = stop.second;
        this->gradientColors.emplace_back(vg::color4f(color.r, color.g, color.b, color.a));
    }
}

void Canvas::setRadialGradient(double cx, double cy, double r, double fx, double fy, const GradientStops& stops, SpreadMethod spread, const Transform& transform)
{
    this->paintType = PaintType::RADIAL_GRADIENT;
    Color icol, ocol;
    if (!stops.empty()) {
        icol = stops[0].second;
        ocol = stops[stops.size() - 1].second;
    } else
        icol = ocol = { 1.f, 1.f, 1.f, 1.f };
    float xform[6]{
        (float)r, 0,
        0, (float)r,
        (float)cx, (float)cy,
    };
    float transform_[6]{
        (float)transform.m00, (float)transform.m10, (float)transform.m01,
        (float)transform.m11, (float)transform.m02, (float)transform.m12,
    };
    float res[6];
    vgutil::multiplyMatrix3(transform_, xform, res);
    float s[2], r_[2];
    vgutil::transformPos2D(0.f, 0.f, res, s);
    vgutil::transformPos2D(0.f, 1.f, res, r_);
    this->gradientParams[0] = s[0];
    this->gradientParams[1] = s[1];
    this->gradientParams[2] = 0.f;
    this->gradientParams[3] = std::max(r_[0], r_[1]) * 2.f;
    this->gradientColors.clear();
    this->gradientStops.clear();
    for (auto& stop : stops) {
        this->gradientStops.emplace_back(stop.first);
        auto& color = stop.second;
        this->gradientColors.emplace_back(vg::color4f(color.r, color.g, color.b, color.a));
    }
}

void Canvas::setTexture(const Canvas* source, TextureType type, const Transform& transform)
{
    // TODO
    this->paintType = PaintType::COLOR;
    this->color = vg::Colors::White;
}

void pathToVG(vg::CommandListRef cl, const Path& path) {
    vg::clBeginPath(cl);
    PathIterator it(path);
    std::array<Point, 3> p;
    while (!it.isDone())
    {
        switch (it.currentSegment(p)) {
        case PathCommand::MoveTo:
            vg::clMoveTo(cl, p[0].x, p[0].y);
            break;
        case PathCommand::LineTo:
            vg::clLineTo(cl, p[0].x, p[0].y);
            break;
        case PathCommand::CubicTo:
            vg::clCubicTo(cl, p[0].x, p[0].y, p[1].x, p[1].y, p[2].x, p[2].y);
            break;
        case PathCommand::Close:
            vg::clClosePath(cl);
            break;
        }

        it.next();
    }
}

void Canvas::fill(const Path& path, const Transform& transform, WindRule winding, BlendMode mode, double opacity)
{
    vg::clPushState(this->cl);
    float transform_[6]{
        (float)transform.m00, (float)transform.m10, (float)transform.m01,
        (float)transform.m11, (float)transform.m02, (float)transform.m12,
    };
    vg::clTransformMult(this->cl, transform_, vg::TransformOrder::Post);
    if (this->latestPath != &path) {
        this->latestPath = &path;
        pathToVG(cl, path);
    }
    static_assert((uint32_t)WindRule::EvenOdd == (uint32_t)vg::FillRule::EvenOdd);
    static_assert((uint32_t)WindRule::NonZero == (uint32_t)vg::FillRule::NonZero);
    auto flags = VG_FILL_FLAGS(vg::PathType::Concave, (uint32_t)winding, true);
    switch (this->paintType) {
        default:
        case PaintType::COLOR:
            vg::clFillPath(
                this->cl,
                this->color,
                flags
            );
            break;
        case PaintType::LINEAR_GRADIENT:
            vg::clFillPath(
                this->cl,
                vg::clCreateLinearGradient(
                    this->cl,
                    this->gradientParams[0],
                    this->gradientParams[1],
                    this->gradientParams[2],
                    this->gradientParams[3],
                    this->gradientColors.data(),
                    this->gradientStops.data(),
                    (uint16_t)this->gradientStops.size()
                ),
                flags
            );
            break;
        case PaintType::RADIAL_GRADIENT:
            vg::clFillPath(
                this->cl,
                vg::clCreateRadialGradient(
                    this->cl,
                    this->gradientParams[0],
                    this->gradientParams[1],
                    this->gradientParams[2],
                    this->gradientParams[3],
                    this->gradientColors.data(),
                    this->gradientStops.data(),
                    (uint16_t)this->gradientStops.size()
                ),
                flags
            );
            break;
    }
    vg::clPopState(this->cl);
}

void Canvas::stroke(const Path& path, const Transform& transform, double width, LineCap cap, LineJoin join, double miterlimit, const DashData& dash, BlendMode mode, double opacity)
{
    vg::clPushState(this->cl);
    float transform_[6]{
        (float)transform.m00, (float)transform.m10, (float)transform.m01,
        (float)transform.m11, (float)transform.m02, (float)transform.m12,
    };
    vg::clTransformMult(this->cl, transform_, vg::TransformOrder::Post);
    if (this->latestPath != &path) {
        this->latestPath = &path;
        pathToVG(cl, path);
    }
    static_assert((uint32_t)LineJoin::Bevel == (uint32_t)vg::LineJoin::Bevel);
    static_assert((uint32_t)LineJoin::Miter == (uint32_t)vg::LineJoin::Miter);
    static_assert((uint32_t)LineJoin::Round == (uint32_t)vg::LineJoin::Round);
    static_assert((uint32_t)LineCap::Butt == (uint32_t)vg::LineCap::Butt);
    static_assert((uint32_t)LineCap::Round == (uint32_t)vg::LineCap::Round);
    static_assert((uint32_t)LineCap::Square == (uint32_t)vg::LineCap::Square);
    auto flags = VG_STROKE_FLAGS((uint32_t)cap, (uint32_t)join, true);
    switch (this->paintType) {
        default:
        case PaintType::COLOR:
            vg::clStrokePath(
                this->cl,
                this->color,
                width,
                flags
            );
            break;
        case PaintType::LINEAR_GRADIENT:
            vg::clStrokePath(
                this->cl,
                vg::clCreateLinearGradient(
                    this->cl,
                    this->gradientParams[0],
                    this->gradientParams[1],
                    this->gradientParams[2],
                    this->gradientParams[3],
                    this->gradientColors.data(),
                    this->gradientStops.data(),
                    (uint16_t)this->gradientStops.size()
                ),
                width,
                flags
            );
            break;
        case PaintType::RADIAL_GRADIENT:
            vg::clStrokePath(
                this->cl,
                vg::clCreateRadialGradient(
                    this->cl,
                    this->gradientParams[0],
                    this->gradientParams[1],
                    this->gradientParams[2],
                    this->gradientParams[3],
                    this->gradientColors.data(),
                    this->gradientStops.data(),
                    (uint16_t)this->gradientStops.size()
                ),
                width,
                flags
            );
            break;
    }
    vg::clPopState(this->cl);
}

void Canvas::blend(const Canvas* source, BlendMode mode, double opacity)
{
    vg::clPushState(this->cl);
    vg::clMulColor(this->cl, vg::color4f(1.f, 1.f, 1.f, (float)opacity));
    vg::clSubmitCommandList(this->cl, source->cl.m_Handle);
    vg::clPopState(this->cl);
}

void Canvas::mask(const Rect& clip, const Transform& transform)
{
    // TODO
    /*auto matrix = to_plutovg_matrix(transform);
    auto path = plutovg_path_create();
    plutovg_path_add_rect(path, clip.x, clip.y, clip.w, clip.h);
    plutovg_path_transform(path, &matrix);
    plutovg_rect(pluto, rect.x, rect.y, rect.w, rect.h);
    plutovg_add_path(pluto, path);
    plutovg_path_destroy(path);

    plutovg_set_source_rgba(pluto, 0, 0, 0, 0);
    plutovg_set_fill_rule(pluto, plutovg_fill_rule_even_odd);
    plutovg_set_operator(pluto, plutovg_operator_src);
    plutovg_set_opacity(pluto, 0.0);
    plutovg_set_matrix(pluto, &translation);
    plutovg_fill(pluto);*/
}

void Canvas::rgba()
{
    // TODO
    /*auto width = plutovg_surface_get_width(surface);
    auto height = plutovg_surface_get_height(surface);
    auto stride = plutovg_surface_get_stride(surface);
    auto data = plutovg_surface_get_data(surface);
    for(int y = 0;y < height;y++)
    {
        auto pixels = reinterpret_cast<uint32_t*>(data + stride * y);
        for(int x = 0;x < width;x++)
        {
            auto pixel = pixels[x];
            auto a = (pixel >> 24) & 0xFF;
            if(a == 0)
                continue;

            auto r = (pixel >> 16) & 0xFF;
            auto g = (pixel >> 8) & 0xFF;
            auto b = (pixel >> 0) & 0xFF;
            if(a != 255)
            {
                r = (r * 255) / a;
                g = (g * 255) / a;
                b = (b * 255) / a;
            }

            pixels[x] = (a << 24) | (b << 16) | (g << 8) | r;
        }
    }*/
}

void Canvas::luminance()
{
    // TODO
    /*auto width = plutovg_surface_get_width(surface);
    auto height = plutovg_surface_get_height(surface);
    auto stride = plutovg_surface_get_stride(surface);
    auto data = plutovg_surface_get_data(surface);
    for(int y = 0;y < height;y++)
    {
        auto pixels = reinterpret_cast<uint32_t*>(data + stride * y);
        for(int x = 0;x < width;x++)
        {
            auto pixel = pixels[x];
            auto r = (pixel >> 16) & 0xFF;
            auto g = (pixel >> 8) & 0xFF;
            auto b = (pixel >> 0) & 0xFF;
            auto l = (2*r + 3*g + b) / 6;

            pixels[x] = l << 24;
        }
    }*/
}

unsigned int Canvas::width() const
{
    return rect.w;
}

unsigned int Canvas::height() const
{
    return rect.h;
}

Rect Canvas::box() const
{
    return rect;
}

} // namespace lunasvg
