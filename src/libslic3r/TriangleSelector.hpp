///|/ Copyright (c) Prusa Research 2019 - 2021 Lukáš Hejl @hejllukas, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef libslic3r_TriangleSelector_hpp_
#define libslic3r_TriangleSelector_hpp_

// #define PRUSASLICER_TRIANGLE_SELECTOR_DEBUG


#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <cfloat>
#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>
#include <cassert>
#include <cinttypes>
#include <cstddef>

#include "Point.hpp"
#include "TriangleMesh.hpp"
#include "admesh/stl.h"

namespace cereal {
class access;
}  // namespace cereal

namespace Slic3r {
class TriangleMesh;

enum class TriangleStateType : int8_t {
    // Maximum is 3. The value is serialized in TriangleSelector into 2 bits.
    NONE      = 0,
    ENFORCER  = 1,
    BLOCKER   = 2,
    // For the fuzzy skin, we use just two values (NONE and FUZZY_SKIN).
    FUZZY_SKIN = ENFORCER,
    // Maximum is 15. The value is serialized in TriangleSelector into 6 bits using a 2 bit prefix code.
    Extruder1 = ENFORCER,
    Extruder2 = BLOCKER,
    Extruder3,
    Extruder4,
    Extruder5,
    Extruder6,
    Extruder7,
    Extruder8,
    Extruder9,
    Extruder10,
    Extruder11,
    Extruder12,
    Extruder13,
    Extruder14,
    Extruder15,
    Count
};

// Following class holds information about selected triangles. It also has power
// to recursively subdivide the triangles and make the selection finer.
class TriangleSelector
{
protected:
    class Triangle;
    struct Vertex;

public:
    enum class CursorType {
        CIRCLE,
        SPHERE,
        POINTER,
        HEIGHT_RANGE
    };

    enum class ForceReselection {
        NO,
        YES
    };

    enum class BucketFillPropagate {
        NO,
        YES
    };

    struct ClippingPlane
    {
        Vec3f normal;
        float offset;
        ClippingPlane() : normal{0.f, 0.f, 1.f}, offset{FLT_MAX} {};
        explicit ClippingPlane(const std::array<float, 4> &clp) : normal{clp[0], clp[1], clp[2]}, offset{clp[3]} {}

        bool is_active() const { return offset != FLT_MAX; }

        bool is_mesh_point_clipped(const Vec3f &point) const { return normal.dot(point) - offset > 0.f; }
    };

    class Cursor
    {
    public:
        Cursor()          = delete;
        virtual ~Cursor() = default;

        float get_edge_limit() { return m_edge_limit; };

        bool is_pointer_in_triangle(const Triangle &tr, const std::vector<Vertex> &vertices) const;
        std::array<Vec3f, 3> transform_triangle(const Triangle &tr, const std::vector<Vertex> &vertices) const;

        virtual bool is_mesh_point_inside(const Vec3f &point) const = 0;
        virtual bool is_pointer_in_triangle(const Vec3f &p1, const Vec3f &p2, const Vec3f &p3) const = 0;
        virtual int  vertices_inside(const Triangle &tr, const std::vector<Vertex> &vertices) const;
        virtual bool is_any_edge_inside_cursor(const Triangle &tr, const std::vector<Vertex> &vertices) const = 0;
        virtual bool is_facet_visible(int facet_idx, const std::vector<Vec3f> &face_normals) const = 0;

        virtual std::vector<int> get_facets_to_select(int facet_idx, const std::vector<Vertex> &vertices, const std::vector<Triangle> &triangles, int orig_size_vertices, int orig_size_indices) const {
            return { facet_idx };
        };

        static bool is_facet_visible(const Cursor &cursor, int facet_idx, const std::vector<Vec3f> &face_normals);

    protected:
        explicit Cursor(const Vec3f &source_, float radius_world, const Transform3d &trafo_, const ClippingPlane &clipping_plane_);

        Transform3f trafo;
        Vec3f       source;

        bool        use_world_coordinates;
        Transform3f trafo_normal;
        float       radius;
        float       radius_sqr;
        Vec3f       dir = Vec3f(0.f, 0.f, 0.f);

        ClippingPlane clipping_plane; // Clipping plane to limit painting to not clipped facets only

        float m_edge_limit;

        friend TriangleSelector;
    };

    class SinglePointCursor : public Cursor
    {
    public:
        SinglePointCursor()           = delete;
        ~SinglePointCursor() override = default;

        bool is_pointer_in_triangle(const Vec3f &p1, const Vec3f &p2, const Vec3f &p3) const override;

        static std::unique_ptr<Cursor> cursor_factory(const Vec3f &center, const Vec3f &camera_pos, const float cursor_radius, const CursorType cursor_type, const Transform3d &trafo_matrix, const ClippingPlane &clipping_plane)
        {
            assert(cursor_type == TriangleSelector::CursorType::CIRCLE || cursor_type == TriangleSelector::CursorType::SPHERE);
            if (cursor_type == TriangleSelector::CursorType::SPHERE)
                return std::make_unique<TriangleSelector::Sphere>(center, camera_pos, cursor_radius, trafo_matrix, clipping_plane);
            else
                return std::make_unique<TriangleSelector::Circle>(center, camera_pos, cursor_radius, trafo_matrix, clipping_plane);
        }

    protected:
        explicit SinglePointCursor(const Vec3f &center_, const Vec3f &source_, float radius_world, const Transform3d &trafo_, const ClippingPlane &clipping_plane_);

        Vec3f center;
    };

    class DoublePointCursor : public Cursor
    {
    public:
        DoublePointCursor()           = delete;
        ~DoublePointCursor() override = default;

        bool is_pointer_in_triangle(const Vec3f &p1, const Vec3f &p2, const Vec3f &p3) const override;

        static std::unique_ptr<Cursor> cursor_factory(const Vec3f &first_center, const Vec3f &second_center, const Vec3f &camera_pos, const float cursor_radius, const CursorType cursor_type, const Transform3d &trafo_matrix, const ClippingPlane &clipping_plane)
        {
            assert(cursor_type == TriangleSelector::CursorType::CIRCLE || cursor_type == TriangleSelector::CursorType::SPHERE);
            if (cursor_type == TriangleSelector::CursorType::SPHERE)
                return std::make_unique<TriangleSelector::Capsule3D>(first_center, second_center, camera_pos, cursor_radius, trafo_matrix, clipping_plane);
            else
                return std::make_unique<TriangleSelector::Capsule2D>(first_center, second_center, camera_pos, cursor_radius, trafo_matrix, clipping_plane);
        }

    protected:
        explicit DoublePointCursor(const Vec3f &first_center_, const Vec3f &second_center_, const Vec3f &source_, float radius_world, const Transform3d &trafo_, const ClippingPlane &clipping_plane_);

        Vec3f first_center;
        Vec3f second_center;
    };

    class Sphere : public SinglePointCursor
    {
    public:
        Sphere() = delete;
        explicit Sphere(const Vec3f &center_, const Vec3f &source_, float radius_world, const Transform3d &trafo_, const ClippingPlane &clipping_plane_)
            : SinglePointCursor(center_, source_, radius_world, trafo_, clipping_plane_){};
        ~Sphere() override = default;

        bool is_mesh_point_inside(const Vec3f &point) const override;
        bool is_any_edge_inside_cursor(const Triangle &tr, const std::vector<Vertex> &vertices) const override;
        bool is_facet_visible(int facet_idx, const std::vector<Vec3f> &face_normals) const override { return true; }
    };

    class Circle : public SinglePointCursor
    {
    public:
        Circle() = delete;
        explicit Circle(const Vec3f &center_, const Vec3f &source_, float radius_world, const Transform3d &trafo_, const ClippingPlane &clipping_plane_)
            : SinglePointCursor(center_, source_, radius_world, trafo_, clipping_plane_){};
        ~Circle() override = default;

        bool is_mesh_point_inside(const Vec3f &point) const override;
        bool is_any_edge_inside_cursor(const Triangle &tr, const std::vector<Vertex> &vertices) const override;
        bool is_facet_visible(int facet_idx, const std::vector<Vec3f> &face_normals) const override {
            return TriangleSelector::Cursor::is_facet_visible(*this, facet_idx, face_normals);
        }
    };

    class Capsule3D : public DoublePointCursor
    {
    public:
        Capsule3D() = delete;
        explicit Capsule3D(const Vec3f &first_center_, const Vec3f &second_center_, const Vec3f &source_, float radius_world, const Transform3d &trafo_, const ClippingPlane &clipping_plane_)
            : TriangleSelector::DoublePointCursor(first_center_, second_center_, source_, radius_world, trafo_, clipping_plane_)
        {}
        ~Capsule3D() override = default;

        bool is_mesh_point_inside(const Vec3f &point) const override;
        bool is_any_edge_inside_cursor(const Triangle &tr, const std::vector<Vertex> &vertices) const override;
        bool is_facet_visible(int facet_idx, const std::vector<Vec3f> &face_normals) const override { return true; }
    };

    class Capsule2D : public DoublePointCursor
    {
    public:
        Capsule2D() = delete;
        explicit Capsule2D(const Vec3f &first_center_, const Vec3f &second_center_, const Vec3f &source_, float radius_world, const Transform3d &trafo_, const ClippingPlane &clipping_plane_)
            : TriangleSelector::DoublePointCursor(first_center_, second_center_, source_, radius_world, trafo_, clipping_plane_)
        {}
        ~Capsule2D() override = default;

        bool is_mesh_point_inside(const Vec3f &point) const override;
        bool is_any_edge_inside_cursor(const Triangle &tr, const std::vector<Vertex> &vertices) const override;
        bool is_facet_visible(int facet_idx, const std::vector<Vec3f> &face_normals) const override {
            return TriangleSelector::Cursor::is_facet_visible(*this, facet_idx, face_normals);
        }
    };

    class HeightRange : public Cursor
    {
    public:
        HeightRange() = delete;

        explicit HeightRange(const Vec3f &mesh_hit, const BoundingBoxf3 &mesh_bbox, float z_range, const Transform3d &trafo, const ClippingPlane &clipping_plane);
        ~HeightRange() override = default;

        bool is_pointer_in_triangle(const Vec3f &p1, const Vec3f &p2, const Vec3f &p3) const override { return false; }
        bool is_mesh_point_inside(const Vec3f &point) const override;
        bool is_any_edge_inside_cursor(const Triangle &tr, const std::vector<Vertex> &vertices) const override;
        bool is_facet_visible(int facet_idx, const std::vector<Vec3f> &face_normals) const override { return true; }

        std::vector<int> get_facets_to_select(int facet_idx, const std::vector<Vertex> &vertices, const std::vector<Triangle> &triangles, int orig_size_vertices, int orig_size_indices) const override;

    private:
        float m_z_range_top;
        float m_z_range_bottom;
    };

    struct TriangleBitStreamMapping
    {
        // Index of the triangle to which we assign the bitstream containing splitting information.
        int triangle_idx        = -1;
        // Index of the first bit of the bitstream assigned to this triangle.
        int bitstream_start_idx = -1;

        TriangleBitStreamMapping() = default;
        explicit TriangleBitStreamMapping(int triangleIdx, int bitstreamStartIdx) : triangle_idx(triangleIdx), bitstream_start_idx(bitstreamStartIdx) {}

        friend bool operator==(const TriangleBitStreamMapping &lhs, const TriangleBitStreamMapping &rhs) { return lhs.triangle_idx == rhs.triangle_idx && lhs.bitstream_start_idx == rhs.bitstream_start_idx; }
        friend bool operator!=(const TriangleBitStreamMapping &lhs, const TriangleBitStreamMapping &rhs) { return !(lhs == rhs); }

    private:
        friend class cereal::access;
        template<class Archive> void serialize(Archive &ar) { ar(triangle_idx, bitstream_start_idx); }
    };

    struct TriangleSplittingData {
        // Vector of triangles and its indexes to the bitstream.
        std::vector<TriangleBitStreamMapping> triangles_to_split;
        // Bit stream containing splitting information.
        std::vector<bool>                     bitstream;
        // Array indicating which triangle state types are used (encoded inside bitstream).
        std::vector<bool>                     used_states { std::vector<bool>(static_cast<size_t>(TriangleStateType::Count), false) };

        TriangleSplittingData() = default;

        friend bool operator==(const TriangleSplittingData &lhs, const TriangleSplittingData &rhs) {
            return lhs.triangles_to_split == rhs.triangles_to_split
                && lhs.bitstream          == rhs.bitstream
                && lhs.used_states        == rhs.used_states;
        }

        friend bool operator!=(const TriangleSplittingData &lhs, const TriangleSplittingData &rhs) { return !(lhs == rhs); }

        // Reset all used states before they are recomputed based on the bitstream.
        void reset_used_states() {
            used_states.resize(static_cast<size_t>(TriangleStateType::Count), false);
            std::fill(used_states.begin(), used_states.end(), false);
        }

        // Update used states based on the bitstream. It just iterated over the bitstream from the bitstream_start_idx till the end.
        void update_used_states(size_t bitstream_start_idx);

    private:
        friend class cereal::access;
        template<class Archive> void serialize(Archive &ar) { ar(triangles_to_split, bitstream, used_states); }
    };

    std::pair<std::vector<Vec3i>, std::vector<Vec3i>> precompute_all_neighbors() const;
    void precompute_all_neighbors_recursive(int facet_idx, const Vec3i &neighbors, const Vec3i &neighbors_propagated, std::vector<Vec3i> &neighbors_out, std::vector<Vec3i> &neighbors_normal_out) const;

    // Set a limit to the edge length, below which the edge will not be split by select_patch().
    // Called by select_patch() internally. Made public for debugging purposes, see TriangleSelectorGUI::render_debug().
    void set_edge_limit(float edge_limit);

    // Create new object on a TriangleMesh. The referenced mesh must
    // stay valid, a ptr to it is saved and used.
    explicit TriangleSelector(const TriangleMesh& mesh);

    // Returns the facet_idx of the unsplit triangle containing the "hit". Returns -1 if the triangle isn't found.
    [[nodiscard]] int select_unsplit_triangle(const Vec3f &hit, int facet_idx) const;
    [[nodiscard]] int select_unsplit_triangle(const Vec3f &hit, int facet_idx, const Vec3i &neighbors) const;

    // Select all triangles fully inside the circle, subdivide where needed.
    void select_patch(int                       facet_start,                   // facet of the original mesh (unsplit) that the hit point belongs to
                      std::unique_ptr<Cursor> &&cursor,                        // Cursor containing information about the point where to start, camera position (mesh coords), matrix to get from mesh to world, and its shape and type.
                      TriangleStateType         new_state,                     // enforcer or blocker?
                      const Transform3d        &trafo_no_translate,            // matrix to get from mesh to world without translation
                      bool                      triangle_splitting,            // If triangles will be split base on the cursor or not
                      float                     highlight_by_angle_deg = 0.f); // The maximal angle of overhang. If it is set to a non-zero value, it is possible to paint only the triangles of overhang defined by this angle in degrees.

    void seed_fill_select_triangles(const Vec3f        &hit,                                       // point where to start
                                    int                 facet_start,                               // facet of the original mesh (unsplit) that the hit point belongs to
                                    const Transform3d  &trafo_no_translate,                        // matrix to get from mesh to world without translation
                                    const ClippingPlane &clp,                                      // Clipping plane to limit painting to not clipped facets only
                                    float               seed_fill_angle,                           // the maximal angle between two facets to be painted by the same color
                                    float               seed_fill_gap_area,                        // The maximal area that will be automatically selected when the surrounding triangles have already been selected.
                                    float               highlight_by_angle_deg = 0.f,              // The maximal angle of overhang. If it is set to a non-zero value, it is possible to paint only the triangles of overhang defined by this angle in degrees.
                                    ForceReselection    force_reselection = ForceReselection::NO); // force reselection of the triangle mesh even in cases that mouse is pointing on the selected triangle

    void bucket_fill_select_triangles(const Vec3f         &hit,                                       // point where to start
                                      int                  facet_start,                               // facet of the original mesh (unsplit) that the hit point belongs to
                                      const ClippingPlane &clp,                                       // Clipping plane to limit painting to not clipped facets only
                                      float                bucket_fill_angle,                         // the maximal angle between two facets to be painted by the same color
                                      float                bucket_fill_gap_area,                      // The maximal area that will be automatically selected when the surrounding triangles have already been selected.
                                      BucketFillPropagate  propagate,                                 // if bucket fill is propagated to neighbor faces or if it fills the only facet of the modified mesh that the hit point belongs to.
                                      ForceReselection     force_reselection = ForceReselection::NO); // force reselection of the triangle mesh even in cases that mouse is pointing on the selected triangle

    bool                 has_facets(TriangleStateType state) const;
    static bool          has_facets(const TriangleSplittingData &data, TriangleStateType test_state);
    int                  num_facets(TriangleStateType state) const;
    // Get facets that pass the filter. Don't triangulate T-joints.
    template<AdditionalMeshInfo facet_info = AdditionalMeshInfo::None>
    typename IndexedTriangleSetType<facet_info>::type get_facets(const std::function<bool(const Triangle &)> &facet_filter) const;
    // Get facets at a given state. Don't triangulate T-joints.
    indexed_triangle_set get_facets(TriangleStateType state) const;
    // Get all facets. Don't triangulate T-joints.
    indexed_triangle_set get_all_facets() const;
    // Get all facets with information about the colors of the facets. Don't triangulate T-joints.
    indexed_triangle_set_with_color get_all_facets_with_colors() const;

    // Get facets that pass the filter. Triangulate T-joints.
    template<AdditionalMeshInfo facet_info = AdditionalMeshInfo::None>
    typename IndexedTriangleSetType<facet_info>::type get_facets_strict(const std::function<bool(const Triangle &)> &facet_filter) const;
    // Get facets at a given state. Triangulate T-joints.
    indexed_triangle_set get_facets_strict(TriangleStateType state) const;
    // Get all facets. Triangulate T-joints.
    indexed_triangle_set get_all_facets_strict() const;
    // Get all facets with information about the colord of the facetd. Triangulate T-joints.
    indexed_triangle_set_with_color get_all_facets_strict_with_colors() const;

    // Get edges around the selected area by seed fill.
    std::vector<Vec2i> get_seed_fill_contour() const;

    // Set facet of the mesh to a given state. Only works for original triangles.
    void set_facet(int facet_idx, TriangleStateType state);

    // Clear everything and make the tree empty.
    void reset();

    // Remove all unnecessary data.
    void garbage_collect();

    // Store the division trees in compact form (a long stream of bits for each triangle of the original mesh).
    // First vector contains pairs of (triangle index, first bit in the second vector).
    TriangleSplittingData serialize() const;

    // Load serialized data. Assumes that correct mesh is loaded.
    void deserialize(const TriangleSplittingData &data, bool needs_reset = true);

    // Extract all used facet states from the given TriangleSplittingData.
    static std::vector<TriangleStateType> extract_used_facet_states(const TriangleSplittingData &data);

    // For all triangles, remove the flag indicating that the triangle was selected by seed fill.
    void seed_fill_unselect_all_triangles();

    // Remove the flag indicating that the triangle was selected by seed fill.
    void seed_fill_unselect_triangle(int facet_idx);

    // For all triangles selected by seed fill, set new TriangleStateType and remove flag indicating that triangle was selected by seed fill.
    // The operation may merge split triangles if they are being assigned the same color.
    void seed_fill_apply_on_triangles(TriangleStateType new_state);

    // For the triangle selected by seed fill, set a new TriangleStateType and remove the flag indicating that the triangle was selected by seed fill.
    // The operation may merge a split triangle if it is being assigned the same color.
    void seed_fill_apply_on_single_triangle(TriangleStateType new_state, const int facet_idx);

    // Compute total area of the triangle.
    double get_triangle_area(const Triangle &triangle) const;

protected:
    // Triangle and info about how it's split.
    class Triangle {
    public:
        // Use TriangleSelector::push_triangle to create a new triangle.
        // It increments/decrements reference counter on vertices.
        Triangle(int a, int b, int c, int source_triangle, const TriangleStateType init_state)
            : verts_idxs{a, b, c},
              source_triangle{source_triangle},
              state{init_state}
        {
            // Initialize bit fields. Default member initializers are not supported by C++17.
            m_selected_by_seed_fill = false;
            m_valid = true;
        }
        // Indices into m_vertices.
        std::array<int, 3> verts_idxs;

        // Index of the source triangle at the initial (unsplit) mesh.
        int source_triangle;

        // Children triangles.
        std::array<int, 4> children;

        // Set the division type.
        void set_division(int sides_to_split, int special_side_idx);

        // Get/set current state.
        void set_state(TriangleStateType type) { assert(! is_split()); state = type; }
        TriangleStateType get_state() const { assert(! is_split()); return state; }

        // Set if the triangle has been selected or unselected by seed fill.
        void select_by_seed_fill() { assert(! is_split()); m_selected_by_seed_fill = true; }
        void unselect_by_seed_fill() { assert(! is_split()); m_selected_by_seed_fill = false; }
        // Get if the triangle has been selected or not by seed fill.
        bool is_selected_by_seed_fill() const { assert(! is_split()); return m_selected_by_seed_fill; }

        // Is this triangle valid or marked to be removed?
        bool valid() const noexcept { return m_valid; }
        // Get info on how it's split.
        bool is_split() const noexcept { return number_of_split_sides() != 0; }
        int number_of_split_sides() const noexcept { return number_of_splits; }
        int special_side() const noexcept { assert(is_split()); return special_side_idx; }

    private:
        friend TriangleSelector;

        // Packing the rest of member variables into 4 bytes, aligned to 4 bytes boundary.
        char number_of_splits { 0 };
        // Index of a vertex opposite to the split edge (for number_of_splits == 1)
        // or index of a vertex shared by the two split edges (for number_of_splits == 2).
        // For number_of_splits == 3, special_side_idx is always zero.
        char special_side_idx { 0 };
        TriangleStateType state;
        bool m_selected_by_seed_fill : 1;
        // Is this triangle valid or marked to be removed?
        bool m_valid : 1;
    };

    struct Vertex {
        explicit Vertex(const stl_vertex& vert)
            : v{vert},
              ref_cnt{0}
        {}
        stl_vertex v;
        int ref_cnt;
    };

    // Lists of vertices and triangles, both original and new
    std::vector<Vertex> m_vertices;
    std::vector<Triangle> m_triangles;
    const TriangleMesh &m_mesh;
    const std::vector<Vec3i> m_neighbors;
    const std::vector<Vec3f> m_face_normals;

    // Number of invalid triangles (to trigger garbage collection).
    int m_invalid_triangles;

    // Limiting length of triangle side (squared).
    float m_edge_limit_sqr = 1.f;

    // Number of original vertices and triangles.
    int m_orig_size_vertices = 0;
    int m_orig_size_indices = 0;

    std::unique_ptr<Cursor> m_cursor;

    // Single triangle selected by seed fill. It is used to optimize painting using a single triangle brush.
    int m_triangle_selected_by_seed_fill = -1;

    // Private functions:
private:
    bool select_triangle(int facet_idx, TriangleStateType type, bool triangle_splitting);
    bool select_triangle_recursive(int facet_idx, const Vec3i &neighbors, TriangleStateType type, bool triangle_splitting);
    void undivide_triangle(int facet_idx);
    void split_triangle(int facet_idx, const Vec3i &neighbors);
    bool remove_useless_children(int facet_idx); // No hidden meaning. Triangles are meant.
    bool is_facet_clipped(int facet_idx, const ClippingPlane &clp) const;
    int  push_triangle(int a, int b, int c, int source_triangle, TriangleStateType state = TriangleStateType::NONE);
    void perform_split(int facet_idx, const Vec3i &neighbors, TriangleStateType old_state);
    Vec3i child_neighbors(const Triangle &tr, const Vec3i &neighbors, int child_idx) const;
    Vec3i child_neighbors_propagated(const Triangle &tr, const Vec3i &neighbors_propagated, int child_idx, const Vec3i &child_neighbors) const;
    // Return child of itriangle at a CCW oriented side (vertexi, vertexj), either first or 2nd part.
    // If itriangle == -1 or if the side sharing (vertexi, vertexj) is not split, return -1.
    enum class Partition {
        First,
        Second,
    };
    int neighbor_child(const Triangle& tr, int vertexi, int vertexj, Partition partition) const;
    int neighbor_child(int itriangle, int vertexi, int vertexj, Partition partition) const;
    int triangle_midpoint(const Triangle& tr, int vertexi, int vertexj) const;
    int triangle_midpoint(int itriangle, int vertexi, int vertexj) const;
    int triangle_midpoint_or_allocate(int itriangle, int vertexi, int vertexj);

    static std::pair<int, int> triangle_subtriangles(const Triangle &tr, int vertexi, int vertexj);
    std::pair<int, int>        triangle_subtriangles(int itriangle, int vertexi, int vertexj) const;

    void append_touching_subtriangles(int itriangle, int vertexi, int vertexj, std::vector<int> &touching_subtriangles_out) const;
    void append_touching_edges(int itriangle, int vertexi, int vertexj, std::vector<Vec2i> &touching_edges_out) const;

    // Returns all triangles that are touching the given facet.
    std::vector<int> get_all_touching_triangles(int facet_idx, const Vec3i &neighbors, const Vec3i &neighbors_propagated) const;

    // Check if the triangle index is the original triangle from mesh, or it was additionally created by splitting.
    bool is_original_triangle(int triangle_idx) const { return triangle_idx < m_orig_size_indices; }

#ifndef NDEBUG
    bool verify_triangle_neighbors(const Triangle& tr, const Vec3i& neighbors) const;
    bool verify_triangle_midpoints(const Triangle& tr) const;
#endif // NDEBUG

    template<AdditionalMeshInfo facet_info>
    void get_facets_strict_recursive(
        const Triangle                              &tr,
        const Vec3i                                 &neighbors,
        const std::function<bool(const Triangle &)> &facet_filter,
        std::vector<stl_triangle_vertex_indices>    &out_triangles,
        std::vector<uint8_t>                        &out_colors) const;

    template<AdditionalMeshInfo facet_info>
    void get_facets_split_by_tjoints(const Vec3i &vertices, const Vec3i &neighbors, uint8_t color, std::vector<stl_triangle_vertex_indices> &out_triangles, std::vector<uint8_t> &out_colors) const;

    void get_seed_fill_contour_recursive(int facet_idx, const Vec3i &neighbors, const Vec3i &neighbors_propagated, std::vector<Vec2i> &edges_out) const;

    bool is_any_neighbor_selected_by_seed_fill(const Triangle &triangle);

    void seed_fill_fill_gaps(const std::vector<int> &gap_fill_candidate_facets, // Facet of the original mesh (unsplit), which needs to be checked if the surrounding gap can be filled (selected).
                             float                   seed_fill_gap_area);       // The maximal area that will be automatically selected when the surrounding triangles have already been selected.

    void bucket_fill_fill_gaps(const std::vector<int>   &gap_fill_candidate_facets, // Facet of the mesh (unsplit), which needs to be checked if the surrounding gap can be filled (selected).
                               float                     bucket_fill_gap_area,      // The maximal area that will be automatically selected when the surrounding triangles have already been selected.
                               TriangleStateType         start_facet_state,         // The state of the starting facet that determines which neighbors to consider.
                               const std::vector<Vec3i> &neighbors,
                               const std::vector<Vec3i> &neighbors_propagate);

    int m_free_triangles_head { -1 };
    int m_free_vertices_head { -1 };
};




} // namespace Slic3r

#endif // libslic3r_TriangleSelector_hpp_
