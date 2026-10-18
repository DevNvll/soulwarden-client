#include "SwcUi/Draw.h"
#include "SwcUi/Model.h"

#include <algorithm>
#include <cmath>

// The layouts of LyShine: the rules of the O3DE layout manager, the row, column, grid, and fitter components, the layout
// cells, and the default cells of the image and text components (O3DE Gems/LyShine, Apache-2.0 OR MIT: facts only, no
// code). The fork adds a fixed row and column (the children keep their sizes) and "visible only" for the grid. The
// fixture tests/ui/fixtures/layouts.lua checks the result against the child offsets that the editor saved.

namespace swc::ui {
namespace {

constexpr float kFitEpsilon = 0.01F;   // a small value that the rules add so that a computed size fits again

float align_offset(int align, float available, float occupied) {
    if (align == kAlignCenter) {
        return (available - occupied) * 0.5F;
    }
    if (align == kAlignRight) {   // also kAlignBottom
        return available - occupied;
    }
    return 0.0F;
}

/** The sizes of the cells along the axis of a row or column: the targets when they fit; else the minimums, then the
 *  rest of the space for the cells under their targets (by the amount that each needs). The space that is left goes
 *  to the cells with an extra ratio, by the ratio. */
std::vector<float> element_sizes(const std::vector<LayoutCellSize>& cells, float available, float spacing) {
    const std::size_t count = cells.size();
    std::vector<float> sizes(count, 0.0F);
    available -= static_cast<float>(count - 1) * spacing;
    float totalTarget = 0.0F;
    for (const LayoutCellSize& cell : cells) {
        totalTarget += cell.target;
    }
    if (totalTarget <= available) {
        for (std::size_t i = 0; i < count; ++i) {
            sizes[i] = cells[i].target;
            available -= cells[i].target;
        }
    } else {
        for (std::size_t i = 0; i < count; ++i) {
            sizes[i] = cells[i].min;
            available -= cells[i].min;
        }
        if (available > 0.0F) {
            float totalNeeded = 0.0F;
            for (std::size_t i = 0; i < count; ++i) {
                totalNeeded += std::max(0.0F, cells[i].target - sizes[i]);
            }
            if (totalNeeded > 0.0F) {
                for (std::size_t i = 0; i < count; ++i) {
                    const float needed = cells[i].target - sizes[i];
                    if (needed > 0.0F) {
                        sizes[i] += needed / totalNeeded * available;
                    }
                }
            }
            available = 0.0F;
        }
    }
    if (available > 0.0F) {
        float smallest = -1.0F;
        for (const LayoutCellSize& cell : cells) {
            if (cell.extra > 0.0F && (smallest < 0.0F || cell.extra < smallest)) {
                smallest = cell.extra;
            }
        }
        if (smallest > 0.0F) {
            float units = 0.0F;
            for (const LayoutCellSize& cell : cells) {
                units += cell.extra > 0.0F ? cell.extra / smallest : 0.0F;
            }
            for (std::size_t i = 0; i < count; ++i) {
                if (cells[i].extra > 0.0F) {
                    sizes[i] += cells[i].extra / smallest * (available / units);
                }
            }
        }
    }
    return sizes;
}

/** The size of a cell across the axis of a row or column: the whole space when the cell takes extra space, else its
 *  target (never less than its minimum). */
float single_size(const LayoutCellSize& cell, float available) {
    if (cell.min > available) {
        return cell.min;
    }
    return cell.extra > 0.0F ? available : std::min(available, cell.target);
}

void set_anchors_zero(TransformData& t) {
    t.anchors = {0.0F, 0.0F, 0.0F, 0.0F};
}

} // namespace

TextMeasure& Model::text_measure() {
    return m_measure != nullptr ? *m_measure : estimate_text_measure();
}

bool Model::is_layout_controller(Id id) const {
    const Element* e = element(id);
    return e != nullptr && (e->layout || e->fitter);
}

std::vector<Id> Model::layout_children(Id id) const {
    std::vector<Id> out;
    const Element* e = element(id);
    if (e == nullptr) {
        return out;
    }
    const bool visibleOnly = e->layout && e->layout->visibleOnly;
    for (const Id child : e->children) {
        const Element* c = element(child);
        if (c != nullptr && (!visibleOnly || c->enabled)) {
            out.push_back(child);
        }
    }
    return out;
}

// ---- the layout cells

bool Model::default_cell(Id id, bool horizontal, int which, float maxSize, float& out) {
    // The default cells of the components of the element (UiLayoutCellDefaultBus): the largest value counts.
    Element* e = element(id);
    if (e == nullptr) {
        return false;
    }
    bool found = false;
    float value = 0.0F;
    const auto take = [&](float v) {
        value = found ? std::max(value, v) : v;
        found = true;
    };
    if (e->layout && !e->layout->fixed) {
        const LayoutData& l = *e->layout;
        const std::vector<Id> children = layout_children(id);
        const float n = static_cast<float>(children.size());
        if (which == 2) {
            take(1.0F);
        } else if (l.kind == LayoutData::Kind::grid) {
            if (which == 0 || children.empty()) {
                take(0.0F);
            } else if (horizontal) {
                int columns = 0;
                if (maxSize >= 0.0F) {
                    const float space = maxSize - (l.padding.left + l.padding.right);
                    const float step = l.cellSize.x + l.spacing.x;
                    if (space > 0.0F) {
                        columns = std::min(step > 0.0F ? static_cast<int>((space + l.spacing.x) / step) : 1, static_cast<int>(n));
                    }
                } else {
                    columns = static_cast<int>(std::ceil(std::sqrt(n)));
                }
                take(columns == 0 ? 0.0F
                                  : l.padding.left + l.padding.right + static_cast<float>(columns) * l.cellSize.x +
                                        static_cast<float>(columns - 1) * l.spacing.x + kFitEpsilon);
            } else {
                const Edges r = rect(id);
                int perRow = 1;
                const float step = l.spacing.x + l.cellSize.x;
                if (step > 0.0F) {
                    perRow += static_cast<int>(std::max(0.0F, (r.right - r.left) - (l.padding.left + l.padding.right + l.cellSize.x)) / step);
                } else {
                    perRow = static_cast<int>(n);
                }
                const int rows = static_cast<int>(std::ceil(n / static_cast<float>(perRow)));
                take(l.padding.top + l.padding.bottom + static_cast<float>(rows) * l.cellSize.y + static_cast<float>(rows - 1) * l.spacing.y);
            }
        } else {
            // A row adds its cells along x and takes the largest along y; a column the other way.
            const bool along = (l.kind == LayoutData::Kind::row) == horizontal;
            float result = 0.0F;
            if (!children.empty()) {
                result = horizontal ? l.padding.left + l.padding.right : l.padding.top + l.padding.bottom;
                float sum = along ? l.spacing.x * (n - 1.0F) : 0.0F;
                float largest = 0.0F;
                for (const Id child : children) {
                    const float v = which == 0 ? (horizontal ? cell_min_width(child, l.ignoreDefaultCells) : cell_min_height(child, l.ignoreDefaultCells))
                                               : (horizontal ? cell_target_width(child, l.ignoreDefaultCells)
                                                             : cell_target_height(child, l.ignoreDefaultCells));
                    sum += v;
                    largest = std::max(largest, v);
                }
                result += along ? sum : largest;
            }
            take(result);
        }
    }
    if (e->image) {
        float v = 0.0F;
        if (which == 2) {
            v = 1.0F;
        } else if (which == 1 && !e->image->sprite.empty()) {
            const Sprite& s = sprite(e->image->sprite);
            if (s.found) {
                Vec2 size{s.width, s.height};
                Edges borders = s.borders;
                if (!s.cells.empty()) {
                    const Sprite::Cell& cell = s.cells[static_cast<std::size_t>(std::clamp(e->image->index, 0, static_cast<int>(s.cells.size()) - 1))];
                    size = {s.width * (cell.uv.right - cell.uv.left), s.height * (cell.uv.bottom - cell.uv.top)};
                    borders = cell.borders;
                }
                if (e->image->type == kImageFixed) {
                    v = horizontal ? size.x : size.y;
                } else if (e->image->type == kImageSliced) {
                    v = horizontal ? borders.left * size.x + (1.0F - borders.right) * size.x : borders.top * size.y + (1.0F - borders.bottom) * size.y;
                } else if (e->image->type == kImageStretchedToFit && !horizontal && size.x > 0.0F) {
                    const Edges r = rect(id);
                    v = size.y * ((r.right - r.left) / size.x);
                }
            }
        }
        take(v);
    }
    if (e->text) {
        float v = 0.0F;
        if (which == 2) {
            v = 1.0F;
        } else if (which == 1) {
            const Vec2 natural = text_natural_size(*this, id, text_measure());
            v = horizontal ? natural.x + (e->text->wrap != kNoWrap ? kFitEpsilon : 0.0F) : natural.y;
        }
        take(v);
    }
    if (found) {
        out = value;
    }
    return found;
}

float Model::cell_min_width(Id id, bool ignoreDefault) {
    const Element* e = element(id);
    if (e != nullptr && e->cell && e->cell->minWidth) {
        return *e->cell->minWidth;
    }
    float value = 0.0F;
    if (!ignoreDefault) {
        default_cell(id, true, 0, -1.0F, value);
    }
    return value;
}

float Model::cell_min_height(Id id, bool ignoreDefault) {
    const Element* e = element(id);
    if (e != nullptr && e->cell && e->cell->minHeight) {
        return *e->cell->minHeight;
    }
    float value = 0.0F;
    if (!ignoreDefault) {
        default_cell(id, false, 0, -1.0F, value);
    }
    return value;
}

float Model::cell_target_width(Id id, bool ignoreDefault) {
    const Element* e = element(id);
    float value = 0.0F;
    if (e != nullptr && e->cell && e->cell->targetWidth) {
        value = *e->cell->targetWidth;
    } else if (!ignoreDefault) {
        default_cell(id, true, 1, -1.0F, value);
    }
    return std::max(value, cell_min_width(id, ignoreDefault));
}

float Model::cell_target_height(Id id, bool ignoreDefault) {
    const Element* e = element(id);
    float value = 0.0F;
    if (e != nullptr && e->cell && e->cell->targetHeight) {
        value = *e->cell->targetHeight;
    } else if (!ignoreDefault) {
        default_cell(id, false, 1, -1.0F, value);
    }
    return std::max(value, cell_min_height(id, ignoreDefault));
}

float Model::cell_extra_width_ratio(Id id, bool ignoreDefault) {
    const Element* e = element(id);
    if (e != nullptr && e->cell && e->cell->extraWidthRatio) {
        return *e->cell->extraWidthRatio;
    }
    float value = 1.0F;
    if (!ignoreDefault) {
        default_cell(id, true, 2, -1.0F, value);
    }
    return value;
}

float Model::cell_extra_height_ratio(Id id, bool ignoreDefault) {
    const Element* e = element(id);
    if (e != nullptr && e->cell && e->cell->extraHeightRatio) {
        return *e->cell->extraHeightRatio;
    }
    float value = 1.0F;
    if (!ignoreDefault) {
        default_cell(id, false, 2, -1.0F, value);
    }
    return value;
}

float Model::fitter_target(Id id, bool horizontal) {
    // The target of the own cell (an override, else the default cells), with no minimum: the canvases that the editor
    // saved show it (a text with a vertical fitter and a minimum height of 25 keeps the height of its text).
    const Element* e = element(id);
    const std::optional<float>& own = e != nullptr && e->cell ? (horizontal ? e->cell->targetWidth : e->cell->targetHeight)
                                                              : std::optional<float>{};
    if (own) {
        return *own;
    }
    float value = 0.0F;
    default_cell(id, horizontal, 1, -1.0F, value);
    return value;
}

std::vector<LayoutCellSize> Model::cell_sizes(Id id, bool horizontal, bool ignoreDefault) {
    std::vector<LayoutCellSize> out;
    for (const Id child : layout_children(id)) {
        LayoutCellSize cell;
        cell.min = horizontal ? cell_min_width(child, ignoreDefault) : cell_min_height(child, ignoreDefault);
        cell.target = horizontal ? cell_target_width(child, ignoreDefault) : cell_target_height(child, ignoreDefault);
        cell.extra = horizontal ? cell_extra_width_ratio(child, ignoreDefault) : cell_extra_height_ratio(child, ignoreDefault);
        out.push_back(cell);
    }
    return out;
}

// ---- applying a layout

void Model::apply_layout_width(Id id) {
    Element* e = element(id);
    if (e == nullptr) {
        return;
    }
    if (e->fitter && e->fitter->horizontal) {
        // The element takes the target width of its own cell; the pivot keeps its place. Stretched anchors join at
        // their middle first (the offsets stay, so the rect moves, as in LyShine).
        const float target = fitter_target(id, true);
        TransformData& t = e->transform;
        if (t.anchors.left != t.anchors.right) {
            t.anchors.left = t.anchors.right = (t.anchors.left + t.anchors.right) * 0.5F;
        }
        const float difference = target - (t.offsets.right - t.offsets.left);
        if (difference != 0.0F) {
            t.offsets.left -= difference * t.pivot.x;
            t.offsets.right += difference * (1.0F - t.pivot.x);
        }
    }
    if (!e->layout) {
        return;
    }
    const LayoutData l = *e->layout;
    const std::vector<Id> children = layout_children(id);
    if (children.empty()) {
        return;
    }
    const Edges r = rect(id);
    const float available = (r.right - r.left) - (l.padding.left + l.padding.right) + kFitEpsilon;
    if (l.kind == LayoutData::Kind::grid) {
        for (const Id child : children) {
            TransformData& t = element(child)->transform;
            set_anchors_zero(t);
            t.offsets = {0.0F, 0.0F, l.cellSize.x, l.cellSize.y};
        }
        return;
    }
    if (l.fixed) {
        // The fork forms: the children keep their sizes; the spacing is the step from the start of one child to the
        // start of the next (no padding, no cells).
        const float width = r.right - r.left;
        for (std::size_t i = 0; i < children.size(); ++i) {
            TransformData& t = element(children[i])->transform;
            const Edges c = rect(children[i]);
            const float w = c.right - c.left;
            set_anchors_zero(t);
            t.offsets.top = c.top - r.top;
            t.offsets.bottom = c.bottom - r.top;
            if (l.kind == LayoutData::Kind::column) {
                t.offsets.left = align_offset(l.hAlign, width, w);
            } else {
                const float step = static_cast<float>(i) * l.spacing.x;
                t.offsets.left = l.order == 1 ? width - step - w : step;
            }
            t.offsets.right = t.offsets.left + w;
        }
        return;
    }
    const std::vector<LayoutCellSize> cells = cell_sizes(id, true, l.ignoreDefaultCells);
    if (l.kind == LayoutData::Kind::column) {
        for (std::size_t i = 0; i < children.size(); ++i) {
            TransformData& t = element(children[i])->transform;
            set_anchors_zero(t);
            const float width = single_size(cells[i], available);
            t.offsets.left = l.padding.left + align_offset(l.hAlign, available, width);
            t.offsets.right = t.offsets.left + width;
        }
        return;
    }
    const std::vector<float> widths = element_sizes(cells, available, l.spacing.x);
    float occupied = static_cast<float>(children.size() - 1) * l.spacing.x;
    for (const float w : widths) {
        occupied += w;
    }
    float x = align_offset(l.hAlign, available, occupied) + l.padding.left + (l.order == 1 ? occupied : 0.0F);
    for (std::size_t i = 0; i < children.size(); ++i) {
        TransformData& t = element(children[i])->transform;
        set_anchors_zero(t);
        if (l.order == 1) {
            t.offsets.right = x;
            x -= widths[i];
            t.offsets.left = x;
            x -= l.spacing.x;
        } else {
            t.offsets.left = x;
            x += widths[i];
            t.offsets.right = x;
            x += l.spacing.x;
        }
    }
}

void Model::apply_layout_height(Id id) {
    Element* e = element(id);
    if (e == nullptr) {
        return;
    }
    if (e->fitter && e->fitter->vertical) {
        const float target = fitter_target(id, false);
        TransformData& t = e->transform;
        if (t.anchors.top != t.anchors.bottom) {
            t.anchors.top = t.anchors.bottom = (t.anchors.top + t.anchors.bottom) * 0.5F;
        }
        const float difference = target - (t.offsets.bottom - t.offsets.top);
        if (difference != 0.0F) {
            t.offsets.top -= difference * t.pivot.y;
            t.offsets.bottom += difference * (1.0F - t.pivot.y);
        }
    }
    if (!e->layout) {
        return;
    }
    const LayoutData l = *e->layout;
    const std::vector<Id> children = layout_children(id);
    if (children.empty()) {
        return;
    }
    const Edges r = rect(id);
    const Vec2 available{(r.right - r.left) - (l.padding.left + l.padding.right) + kFitEpsilon,
                         (r.bottom - r.top) - (l.padding.top + l.padding.bottom) + kFitEpsilon};
    if (l.kind == LayoutData::Kind::grid) {
        const int n = static_cast<int>(children.size());
        int columns = 0;
        int rows = 0;
        if (l.startingWith == 0) {
            columns = std::clamp(static_cast<int>(std::floor((available.x + l.spacing.x) / (l.cellSize.x + l.spacing.x))), 1, n);
            rows = std::max(1, static_cast<int>(std::ceil(static_cast<float>(n) / static_cast<float>(columns))));
        } else {
            rows = std::clamp(static_cast<int>(std::floor((available.y + l.spacing.y) / (l.cellSize.y + l.spacing.y))), 1, n);
            columns = std::max(1, static_cast<int>(std::ceil(static_cast<float>(n) / static_cast<float>(rows))));
        }
        const Vec2 occupied{static_cast<float>(columns) * l.cellSize.x + l.spacing.x * static_cast<float>(columns - 1),
                            static_cast<float>(rows) * l.cellSize.y + l.spacing.y * static_cast<float>(rows - 1)};
        const float hOffset = align_offset(l.hAlign, available.x, occupied.x);
        const float vOffset = align_offset(l.vAlign, available.y, occupied.y);
        // The placement uses its own count of columns or rows (no limit by the number of children), as LyShine does.
        const int placeColumns = std::max(1, static_cast<int>(std::floor((available.x + l.spacing.x) / (l.cellSize.x + l.spacing.x))));
        const int placeRows = std::max(1, static_cast<int>(std::floor((available.y + l.spacing.y) / (l.cellSize.y + l.spacing.y))));
        for (int i = 0; i < n; ++i) {
            const int column = l.startingWith == 0 ? i % placeColumns : i / placeRows;
            const int row = l.startingWith == 0 ? i / placeColumns : i % placeRows;
            TransformData& t = element(children[static_cast<std::size_t>(i)])->transform;
            set_anchors_zero(t);
            if (l.horizontalOrder == 1) {
                t.offsets.right = l.padding.left + occupied.x - static_cast<float>(column) * (l.cellSize.x + l.spacing.x);
                t.offsets.left = t.offsets.right - l.cellSize.x;
            } else {
                t.offsets.left = l.padding.left + static_cast<float>(column) * (l.cellSize.x + l.spacing.x);
                t.offsets.right = t.offsets.left + l.cellSize.x;
            }
            if (l.order == 1) {
                t.offsets.bottom = l.padding.top + occupied.y - static_cast<float>(row) * (l.cellSize.y + l.spacing.y);
                t.offsets.top = t.offsets.bottom - l.cellSize.y;
            } else {
                t.offsets.top = l.padding.top + static_cast<float>(row) * (l.cellSize.y + l.spacing.y);
                t.offsets.bottom = t.offsets.top + l.cellSize.y;
            }
            t.offsets.left += hOffset;
            t.offsets.right += hOffset;
            t.offsets.top += vOffset;
            t.offsets.bottom += vOffset;
        }
        return;
    }
    if (l.fixed) {
        const float height = r.bottom - r.top;
        for (std::size_t i = 0; i < children.size(); ++i) {
            TransformData& t = element(children[i])->transform;
            const float h = t.offsets.bottom - t.offsets.top;
            if (l.kind == LayoutData::Kind::row) {
                t.offsets.top = align_offset(l.vAlign, height, h);
            } else {
                const float step = static_cast<float>(i) * l.spacing.x;
                t.offsets.top = l.order == 1 ? height - step - h : step;
            }
            t.offsets.bottom = t.offsets.top + h;
        }
        return;
    }
    const std::vector<LayoutCellSize> cells = cell_sizes(id, false, l.ignoreDefaultCells);
    if (l.kind == LayoutData::Kind::row) {
        for (std::size_t i = 0; i < children.size(); ++i) {
            TransformData& t = element(children[i])->transform;
            const float height = single_size(cells[i], available.y);
            t.offsets.top = l.padding.top + align_offset(l.vAlign, available.y, height);
            t.offsets.bottom = t.offsets.top + height;
        }
        return;
    }
    const std::vector<float> heights = element_sizes(cells, available.y, l.spacing.x);
    float occupied = static_cast<float>(children.size() - 1) * l.spacing.x;
    for (const float h : heights) {
        occupied += h;
    }
    float y = align_offset(l.vAlign, available.y, occupied) + l.padding.top + (l.order == 1 ? occupied : 0.0F);
    for (std::size_t i = 0; i < children.size(); ++i) {
        TransformData& t = element(children[i])->transform;
        if (l.order == 1) {
            t.offsets.bottom = y;
            y -= heights[i];
            t.offsets.top = y;
            y -= l.spacing.x;
        } else {
            t.offsets.top = y;
            y += heights[i];
            t.offsets.bottom = y;
            y += l.spacing.x;
        }
    }
}

// ---- the layout manager

void Model::compute_layout(Id id) {
    // The controllers below the element, parents before children (depth first), then all widths, then all heights.
    std::vector<Id> controllers;
    std::vector<Id> stack;
    if (const Element* e = element(id)) {
        stack.assign(e->children.rbegin(), e->children.rend());
    }
    while (!stack.empty()) {
        const Id next = stack.back();
        stack.pop_back();
        const Element* e = element(next);
        if (e == nullptr) {
            continue;
        }
        if (e->layout || e->fitter) {
            controllers.push_back(next);
        }
        stack.insert(stack.end(), e->children.rbegin(), e->children.rend());
    }
    apply_layout_width(id);
    for (const Id c : controllers) {
        apply_layout_width(c);
    }
    apply_layout_height(id);
    for (const Id c : controllers) {
        apply_layout_height(c);
    }
    // The sizes after the computation: a later change of size marks the layout again.
    for (const Id c : controllers) {
        note_layout_element(c);
    }
    note_layout_element(id);
}

void Model::note_layout_element(Id id) {
    const Element* e = element(id);
    if (e == nullptr || !e->layout) {
        return;
    }
    const Edges r = rect(id);
    m_layoutElements.insert(id);
    m_layoutSizes[id] = {r.right - r.left, r.bottom - r.top};
}

void Model::add_layout_mark(Id id) {
    if (std::find(m_layoutMarks.begin(), m_layoutMarks.end(), id) != m_layoutMarks.end()) {
        return;
    }
    const auto is_ancestor = [this](Id ancestor, Id of) {
        for (const Element* e = element(of); e != nullptr && e->parent != kNoId; e = element(e->parent)) {
            if (e->parent == ancestor) {
                return true;
            }
        }
        return false;
    };
    for (const Id mark : m_layoutMarks) {
        if (is_ancestor(mark, id)) {
            return;
        }
    }
    m_layoutMarks.erase(std::remove_if(m_layoutMarks.begin(), m_layoutMarks.end(), [&](Id mark) { return is_ancestor(id, mark); }),
                        m_layoutMarks.end());
    m_layoutMarks.push_back(id);
}

void Model::mark_layout(Id id) {
    if (is_layout_controller(id)) {
        add_layout_mark(id);
    }
}

void Model::child_list_changed(Id parent) {
    if (element(parent) == nullptr) {
        return;
    }
    mark_layout(parent);
    mark_layout_cell_change(parent, false);
}

void Model::mark_layout_cell_change(Id id, bool defaultCell) {
    Id top = kNoId;
    const Element* e = element(id);
    for (Id parent = e != nullptr ? e->parent : kNoId; parent != kNoId;) {
        const Element* p = element(parent);
        if (p == nullptr || !p->layout || p->layout->fixed || p->layout->kind == LayoutData::Kind::grid) {
            break;   // only a row or a column uses the cells of its children
        }
        if (defaultCell && p->layout->ignoreDefaultCells) {
            break;
        }
        top = parent;
        parent = p->parent;
    }
    if (top != kNoId) {
        add_layout_mark(top);
    }
}

int Model::update_layouts() {
    // A layout whose size changed (or that has no size yet) computes again (OnCanvasSpaceRectChanged).
    for (auto it = m_layoutElements.begin(); it != m_layoutElements.end();) {
        const Element* e = element(*it);
        if (e == nullptr || !e->layout) {
            m_layoutSizes.erase(*it);
            it = m_layoutElements.erase(it);
            continue;
        }
        const auto canvas = m_canvases.find(e->canvas);
        if (canvas == m_canvases.end() || !canvas->second.enabled) {
            ++it;
            continue;
        }
        const Edges r = rect(*it);
        const auto size = m_layoutSizes.find(*it);
        if (size == m_layoutSizes.end() || std::fabs(size->second.x - (r.right - r.left)) > 0.05F ||
            std::fabs(size->second.y - (r.bottom - r.top)) > 0.05F) {
            add_layout_mark(*it);
        }
        ++it;
    }
    std::vector<Id> marks;
    marks.swap(m_layoutMarks);
    for (const Id id : marks) {
        const Element* e = element(id);
        if (e == nullptr) {
            continue;
        }
        const auto canvas = m_canvases.find(e->canvas);
        if (canvas != m_canvases.end() && !canvas->second.enabled) {
            m_layoutMarks.push_back(id);   // a hidden canvas does not update: the mark stays
            continue;
        }
        compute_layout(id);
    }
    return static_cast<int>(marks.size());
}

Vec2 Model::size_to_fit(Id id, Vec2 child, int count) {
    // UiLayoutBus GetSizeToFitChildElements (the dynamic layout): a row adds the widths of the children with the
    // spacing and the padding; across, the child size when the anchors are together, else the current size. A column
    // the other way; a grid the box of its rows and columns with the padding.
    const Element* e = element(id);
    const Edges r = rect(id);
    Vec2 size{r.right - r.left, r.bottom - r.top};
    if (e == nullptr || !e->layout) {
        return size;
    }
    const LayoutData& l = *e->layout;
    const float n = static_cast<float>(count);
    const TransformData& t = e->transform;
    if (l.kind == LayoutData::Kind::grid) {
        if (count <= 0) {
            return size;
        }
        const float spaceX = size.x - (l.padding.left + l.padding.right) + kFitEpsilon;
        const float spaceY = size.y - (l.padding.top + l.padding.bottom) + kFitEpsilon;
        int columns = 1;
        int rows = 1;
        if (l.startingWith == 0) {
            columns = std::clamp(static_cast<int>(std::floor((spaceX + l.spacing.x) / (child.x + l.spacing.x))), 1, count);
            rows = std::max(1, static_cast<int>(std::ceil(n / static_cast<float>(columns))));
        } else {
            rows = std::clamp(static_cast<int>(std::floor((spaceY + l.spacing.y) / (child.y + l.spacing.y))), 1, count);
            columns = std::max(1, static_cast<int>(std::ceil(n / static_cast<float>(rows))));
        }
        return {static_cast<float>(columns) * child.x + l.spacing.x * static_cast<float>(columns - 1) + l.padding.left + l.padding.right + kFitEpsilon,
                static_cast<float>(rows) * child.y + l.spacing.y * static_cast<float>(rows - 1) + l.padding.top + l.padding.bottom + kFitEpsilon};
    }
    const bool row = l.kind == LayoutData::Kind::row;
    const float along = count > 0 ? (row ? child.x : child.y) * n + l.spacing.x * (n - 1.0F) +
                                         (row ? l.padding.left + l.padding.right : l.padding.top + l.padding.bottom)
                                   : 0.0F;
    const bool togetherAcross = row ? t.anchors.top == t.anchors.bottom : t.anchors.left == t.anchors.right;
    const float across = togetherAcross ? (count > 0 ? (row ? child.y : child.x) : 0.0F) : (row ? size.y : size.x);
    return row ? Vec2{along, across} : Vec2{across, along};
}

} // namespace swc::ui
