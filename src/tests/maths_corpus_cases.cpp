// NOT ORIGINAL - thug2_maths_check's corpus cases for base/maths (the game's own maths: every function in its headers)
// and for D3DX9's maths that base/maths/view_transforms.h and maths::matrix_multiply replaced (maths_corpus.h).
// Inputs of the game's scales: coordinates within 5000 inches, unit vectors, rotations (from unit quaternions) with a
// translation, angles within two turns.

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>
#include <span>
#include <string>
#include <utility>

#include "base/core/checked_index.h"
#include "base/maths/mat4.h"
#include "base/maths/maths.h"
#include "base/maths/quat_rotation.h"
#include "base/maths/random.h"
#include "base/maths/rotation_blend.h"
#include "base/maths/shape_tests.h"
#include "base/maths/trig.h"
#include "base/maths/vec4.h"
#include "base/maths/view_transforms.h"
#include "tests/maths_check_d3dx.h"
#include "tests/maths_corpus.h"

namespace tests {

namespace {

using maths::Mat4;
using maths::Vec4;

constexpr float kCoordinateRange = 5000.0F;
constexpr float kAngleRange = 7.0F;
constexpr float kQuarterTurn = std::numbers::pi_v<float> / 2.0F;
constexpr float kPositiveMax = 1000.0F;
constexpr double kWideDouble = 1.0e19; // (past 2^63 both ways: truncate_to_i64's out-of-range answer)
constexpr int kRandomLimitMax = 1 << 16;
constexpr float kTinyScale = 1.0e-20F;
constexpr int kAxes = 3;
constexpr float kSkew = 0.1F;          // how far the rows are pushed off square
constexpr float kSmallTurn = 0.03F;    // radians: below the rotation blender's threshold
constexpr float kQuatLengthMax = 2.0F; // the lengths QuatRotation::make_unit is given
constexpr float kSizeMin = 0.01F;      // a view's width / height, a projective w
constexpr float kSizeMax = 2000.0F;

// The inputs, drawn through the corpus (read back when comparing). One draw per argument list: the order a call's
// arguments are worked out in is the compiler's.
class Draw {
public:
    Draw(Corpus& corpus, uint64_t seed) : corpus_(&corpus), random_(seed) {}
    float coordinate() { return corpus_->in(coordinate_(random_)); }
    float unit() { return corpus_->in(unit_(random_)); }
    float angle() { return corpus_->in(angle_(random_)); }
    float positive() { return corpus_->in(positive_(random_)); }
    float within(float low, float high)
    {
        return corpus_->in(std::uniform_real_distribution<float>(low, high)(random_));
    }
    double wide() { return corpus_->in(std::uniform_real_distribution<double>(-kWideDouble, kWideDouble)(random_)); }
    double angle_double() { return corpus_->in(static_cast<double>(angle_(random_))); }
    int32_t integer(int32_t low, int32_t high)
    {
        return corpus_->in(std::uniform_int_distribution<int32_t>(low, high)(random_));
    }
    uint32_t bits() { return corpus_->in(std::uniform_int_distribution<uint32_t>()(random_)); }
    Vec4 point() { return {.x = coordinate(), .y = coordinate(), .z = coordinate(), .w = 1.0F}; }
    Vec4 vector() { return {.x = unit(), .y = unit(), .z = unit(), .w = unit()}; }
    // A unit vector, w 0. (Worked out here and then handed to the corpus as inputs: the square root is the C runtime's,
    // which on the 32-bit build follows the x87's precision.)
    Vec4 direction()
    {
        const Vec4 v{.x = unit_(random_), .y = unit_(random_), .z = unit_(random_), .w = 0.0F};
        const double length = std::sqrt((static_cast<double>(v.x) * v.x) + (static_cast<double>(v.y) * v.y) +
                                        (static_cast<double>(v.z) * v.z));
        const bool usable = length > 0.0;
        return {.x = corpus_->in(usable ? static_cast<float>(v.x / length) : 0.0F),
                .y = corpus_->in(usable ? static_cast<float>(v.y / length) : 1.0F),
                .z = corpus_->in(usable ? static_cast<float>(v.z / length) : 0.0F),
                .w = 0.0F};
    }
    // A rotation (a unit quaternion's matrix, in double precision; the corpus's inputs are its floats) and a translation
    // of coordinates.
    Mat4 placement()
    {
        std::array<double, 4> quaternion{};
        double length_sqr = 0.0;
        for (double& value : quaternion) {
            value = unit_(random_);
            length_sqr += value * value;
        }
        const double scale = length_sqr > 0.0 ? 2.0 / length_sqr : 0.0;
        const double x = core::at(quaternion, 0);
        const double y = core::at(quaternion, 1);
        const double z = core::at(quaternion, 2);
        const double w = core::at(quaternion, 3);
        auto rounded = [this](double value) { return corpus_->in(static_cast<float>(value)); };
        Mat4 matrix{};
        core::at(matrix.row, 0) = {.x = rounded(1.0 - (scale * ((y * y) + (z * z)))),
                                   .y = rounded(scale * ((x * y) + (w * z))),
                                   .z = rounded(scale * ((x * z) - (w * y))),
                                   .w = 0.0F};
        core::at(matrix.row, 1) = {.x = rounded(scale * ((x * y) - (w * z))),
                                   .y = rounded(1.0 - (scale * ((x * x) + (z * z)))),
                                   .z = rounded(scale * ((y * z) + (w * x))),
                                   .w = 0.0F};
        core::at(matrix.row, 2) = {.x = rounded(scale * ((x * z) + (w * y))),
                                   .y = rounded(scale * ((y * z) - (w * x))),
                                   .z = rounded(1.0 - (scale * ((x * x) + (y * y)))),
                                   .w = 0.0F};
        core::at(matrix.row, 3) = point();
        return matrix;
    }
    // Any matrix: 16 unit values, the translation of coordinates.
    Mat4 general()
    {
        Mat4 matrix{};
        for (int k = 0; k < 4; ++k) {
            core::at(matrix.row, k) =
                k == 3 ? Vec4{.x = coordinate(), .y = coordinate(), .z = coordinate(), .w = unit()} : vector();
        }
        return matrix;
    }

private:
    Corpus* corpus_;
    std::mt19937_64 random_;
    std::uniform_real_distribution<float> coordinate_{-kCoordinateRange, kCoordinateRange};
    std::uniform_real_distribution<float> unit_{-1.0F, 1.0F};
    std::uniform_real_distribution<float> angle_{-kAngleRange, kAngleRange};
    std::uniform_real_distribution<float> positive_{0.0F, kPositiveMax};
};

// The results, handed to the corpus.
class Results {
public:
    Results(Corpus& corpus, CorpusGroup& group) : corpus_(&corpus), group_(&group) {}
    CorpusTally& tally(std::string name, char unit) { return group_->tally(std::move(name), unit); }
    template <CorpusValue Value>
    void put(CorpusTally& tally, Value value, double input)
    {
        corpus_->out(tally, value, input);
    }
    void put(CorpusTally& tally, const Vec4& v, double input)
    {
        corpus_->outs<float>(tally, std::span<const float>(v.data(), 4), input);
    }
    void put(CorpusTally& tally, const Mat4& matrix, double input)
    {
        corpus_->outs<float>(tally, matrix.floats(), input);
    }

private:
    Corpus* corpus_;
    CorpusGroup* group_;
};

// maths.h, trig.h, random.h
void scalar_cases(Corpus& corpus, CorpusDraws draws)
{
    const int count = draws.count;
    corpus.section("base/maths scalars");
    CorpusGroup group(corpus,
                      "(a) base/maths, the scalars (maths.h, trig.h, random.h): this build vs the 32-bit build");
    Results results(corpus, group);
    Draw draw(corpus, draws.seed);
    CorpusTally& t_confine = results.tally("confine", 'f');
    CorpusTally& t_sign = results.tally("get_sign", 'f');
    CorpusTally& t_acos = results.tally("fast_a_cos", 'f');
    CorpusTally& t_atan2 = results.tally("fast_atan2", 'f');
    CorpusTally& t_atan2_unit = results.tally("fast_atan2 (unit)", 'f');
    CorpusTally& t_keep = results.tally("keep_within", 'f');
    CorpusTally& t_rotate_2d = results.tally("create_rotate_2d", 'f');
    CorpusTally& t_fast_sin = results.tally("fast_sin", 'f');
    CorpusTally& t_fast_cos = results.tally("fast_cos", 'f');
    CorpusTally& t_round_f = results.tally("round_non_negative<int>(float)", 'u');
    CorpusTally& t_round_d = results.tally("round_non_negative<int64>(double)", 'u');
    CorpusTally& t_spin = results.tally("spin_by (trig.h)", 'd');
    CorpusTally& t_acos_f = results.tally("acos_f (trig.h)", 'f');
    CorpusTally& t_acos_f_above = results.tally("acos_f's comparison", 'b');
    CorpusTally& t_nac = results.tally("neg_asin_cos (trig.h)", 'f');
    CorpusTally& t_nac_above = results.tally("neg_asin_cos's comparison", 'b');
    CorpusTally& t_nac_inverse = results.tally("neg_asin_cos 1 / cos", 'd');
    CorpusTally& t_truncate = results.tally("truncate_to_i64", 'u');
    CorpusTally& t_rnd = results.tally("rnd, roll_side_stream", 'u');
    CorpusTally& t_spread = results.tally("signed_spread", 'f');
    CorpusTally& t_spread_exact = results.tally("plus_or_minus_exact", 'd');
    maths::fill_sine_table();
    for (int i = 0; i < count; ++i) {
        const float value = draw.coordinate();
        const float low = draw.coordinate();
        const float high = draw.coordinate();
        results.put(t_confine, maths::confine(value, low, high), value);
        const float u = draw.unit();
        results.put(t_sign, maths::get_sign(u), u);
        results.put(t_acos, maths::fast_a_cos(u), u);
        results.put(t_atan2, maths::fast_atan2(value, low), value);
        const float v = draw.unit();
        results.put(t_atan2_unit, maths::fast_atan2(u, v), u);
        results.put(t_keep, maths::keep_within(value, {.target = low, .distance = high}), value);
        const float turn = draw.angle();
        std::array<float, 4> rotation{};
        maths::create_rotate_2d(rotation, turn);
        corpus.outs<float>(t_rotate_2d, rotation, turn);
        const float quarter = draw.within(0.0F, kQuarterTurn);
        results.put(t_fast_sin, maths::fast_sin(quarter), quarter);
        results.put(t_fast_cos, maths::fast_cos(quarter), quarter);
        const float amount = draw.positive();
        results.put(t_round_f, maths::round_non_negative<int32_t>(amount), amount);
        results.put(t_round_d, maths::round_non_negative<int64_t>(static_cast<double>(amount) * amount), amount);
        const double angle = draw.angle_double();
        double spun_a = 0.0;
        double spun_b = 0.0;
        maths::spin_by(angle, value, low, &spun_a, &spun_b);
        results.put(t_spin, spun_a, angle);
        results.put(t_spin, spun_b, angle);
        bool above = false;
        const float limit = draw.within(0.0F, 3.0F);
        results.put(t_acos_f, maths::acos_f({.value = u, .limit = limit}, &above), u);
        results.put(t_acos_f_above, above, u);
        maths::Inverses inverses{.inverse = 0.0, .negative = 0.0};
        const float small_limit = draw.within(0.0F, 1.0F);
        results.put(t_nac, maths::neg_asin_cos({.value = u, .limit = small_limit}, &above, &inverses), u);
        results.put(t_nac_above, above, u);
        results.put(t_nac_inverse, inverses.inverse, u);
        results.put(t_nac_inverse, inverses.negative, u);
        const double wide = draw.wide();
        results.put(t_truncate, maths::truncate_to_i64(wide), wide);
        results.put(t_truncate, maths::truncate_to_i64(static_cast<double>(value) * high), value);
        const uint32_t seed_value = draw.bits();
        const int32_t random_limit = draw.integer(1, kRandomLimitMax);
        maths::seed_random(seed_value);
        results.put(t_rnd, maths::rnd(random_limit), random_limit);
        results.put(t_rnd, maths::roll_side_stream(random_limit), random_limit);
        results.put(t_spread, maths::signed_spread(value), value);
        results.put(t_spread_exact, maths::plus_or_minus_exact(value), value);
    }
    corpus.section("base/maths scalars end");
}

// vec4.h (the vectors and segments)
void vector_cases(Corpus& corpus, CorpusDraws draws)
{
    const int count = draws.count;
    corpus.section("base/maths vectors");
    CorpusGroup group(corpus, "(here) base/maths, the vectors (vec4.h): this build vs the 32-bit build");
    Results results(corpus, group);
    Draw draw(corpus, draws.seed);
    CorpusTally& t_arithmetic = results.tally("+ - * / += -= *= /= -v", 'f');
    CorpusTally& t_equal = results.tally("== !=", 'b');
    CorpusTally& t_flatten = results.tally("flatten_onto_plane", 'f');
    CorpusTally& t_align = results.tally("align_to_plane", 'f');
    CorpusTally& t_heading = results.tally("change_heading", 'f');
    CorpusTally& t_project = results.tally("project_onto", 'f');
    CorpusTally& t_dominant = results.tally("dominant_axis_index", 'f');
    CorpusTally& t_dominant_axis = results.tally("dominant_axis_index's axis", 'u');
    CorpusTally& t_length = results.tally("magnitude_squared, len_of", 'f');
    CorpusTally& t_unit = results.tally("make_unit", 'f');
    CorpusTally& t_set_length = results.tally("set_length", 'f');
    CorpusTally& t_quarter = results.tally("turn_quarter_about_y", 'f');
    CorpusTally& t_units = results.tally("degree/radian/feet conversions", 'f');
    CorpusTally& t_x90 = results.tally("negate_x_rotate_x90", 'f');
    CorpusTally& t_rotate_x = results.tally("rotate_about_x", 'f');
    CorpusTally& t_rotate_y = results.tally("rotate_about_y", 'f');
    CorpusTally& t_rotate_z = results.tally("rotate_about_z", 'f');
    CorpusTally& t_rotate_axis = results.tally("rotate_about_axis", 'f');
    CorpusTally& t_multiply = results.tally("multiply_by (Mat4)", 'f');
    CorpusTally& t_dot = results.tally("dot_vec", 'f');
    CorpusTally& t_cross = results.tally("cross_vec", 'f');
    CorpusTally& t_distance = results.tally("get_distance", 'd');
    CorpusTally& t_distance_sqr = results.tally("get_distance_squared", 'd');
    CorpusTally& t_beyond = results.tally("is_angle_beyond", 'b');
    CorpusTally& t_turn = results.tally("read_turn_angle", 'd');
    CorpusTally& t_turn_axis = results.tally("get_turn_around_axis", 'd');
    CorpusTally& t_to_point = results.tally("Seg::distance_sqr_to_point", 'f');
    CorpusTally& t_to_line = results.tally("Seg::distance_sqr_to_line", 'd');
    CorpusTally& t_to_line_st = results.tally("Seg::distance_sqr_to_line along_s, t", 'f');
    CorpusTally& t_closest = results.tally("closest_points_between_lines", 'f');
    CorpusTally& t_closest_found = results.tally("closest_points_between_lines found", 'b');
    for (int i = 0; i < count; ++i) {
        const Vec4 here = draw.point();
        const Vec4 step = draw.vector();
        const Vec4 normal = draw.direction();
        const float scale = draw.unit();
        const double input = here.x;
        results.put(t_arithmetic, here + step, input);
        results.put(t_arithmetic, here - step, input);
        results.put(t_arithmetic, here * scale, input);
        results.put(t_arithmetic, scale * step, input);
        results.put(t_arithmetic, here / scale, input);
        results.put(t_arithmetic, -step, input);
        Vec4 sum = here;
        sum += step;
        sum -= normal;
        sum *= scale;
        sum /= step.y;
        results.put(t_arithmetic, sum, input);
        results.put(t_equal, here == step, input);
        results.put(t_equal, here != sum, input);
        Vec4 flat = here;
        results.put(t_flatten, flat.flatten_onto_plane(normal), input);
        Vec4 aligned = here;
        results.put(t_align, aligned.align_to_plane(normal), input);
        Vec4 headed = here;
        results.put(t_heading, headed.change_heading(normal), input);
        Vec4 projected = here;
        results.put(t_project, projected.project_onto(normal), input);
        int axis = 0;
        results.put(t_dominant, here.dominant_axis_index(&axis), input);
        results.put(t_dominant_axis, static_cast<int32_t>(axis), input);
        results.put(t_length, here.magnitude_squared(), input);
        results.put(t_length, here.len_of(), input);
        Vec4 unit = here;
        results.put(t_unit, unit.make_unit(), input);
        Vec4 lengthened = step;
        results.put(t_set_length, *lengthened.set_length(draw.coordinate()), input);
        Vec4 quarter = here;
        results.put(t_quarter, quarter.turn_quarter_about_y(draw.integer(0, 4)), input);
        Vec4 converted = here;
        converted.degree_to_radian_conv();
        results.put(t_units, converted, input);
        converted = step;
        converted.convert_to_degrees();
        results.put(t_units, converted, input);
        converted = step;
        converted.feet_as_inches();
        results.put(t_units, converted, input);
        converted = here;
        converted.negate_x_rotate_x90();
        results.put(t_x90, converted, input);
        const float angle = draw.angle();
        Vec4 turned = here;
        results.put(t_rotate_x, turned.rotate_about_x(angle), angle);
        turned = here;
        results.put(t_rotate_y, turned.rotate_about_y(angle), angle);
        turned = here;
        results.put(t_rotate_z, turned.rotate_about_z(angle), angle);
        turned = here;
        results.put(t_rotate_axis, turned.rotate_about_axis(normal, angle), angle);
        const Mat4 placement = draw.placement();
        Vec4 multiplied = here;
        results.put(t_multiply, multiplied.multiply_by(placement), input);
        results.put(t_dot, maths::dot_vec(here, step), input);
        results.put(t_dot, maths::dot_vec(normal, step), input);
        results.put(t_cross, maths::cross_vec(here, step), input);
        const Vec4 there = draw.point();
        results.put(t_distance, maths::get_distance(here, there), input);
        results.put(t_distance_sqr, maths::get_distance_squared(here, there), input);
        const float degrees = draw.within(0.0F, 180.0F);
        results.put(t_beyond, maths::is_angle_beyond(here, there, degrees), degrees);
        results.put(t_turn, maths::read_turn_angle(here, there), input);
        results.put(t_turn_axis, maths::get_turn_around_axis(here, there, normal), input);
        const maths::Seg segment(here, there);
        const Vec4 point = draw.point();
        Vec4 offset{};
        results.put(t_to_point, segment.distance_sqr_to_point(point, &offset), input);
        results.put(t_to_point, offset, input);
        const maths::Seg other(point, draw.point());
        float along_s = 0.0F;
        float along_t = 0.0F;
        results.put(t_to_line, segment.distance_sqr_to_line(other, &along_s, &along_t), input);
        results.put(t_to_line_st, along_s, input);
        results.put(t_to_line_st, along_t, input);
        for (const bool clamp : {false, true}) {
            Vec4 point_a{};
            Vec4 point_b{};
            float along_a = 0.0F;
            float along_b = 0.0F;
            results.put(t_closest_found,
                        maths::closest_points_between_lines(segment, other, point_a, point_b, along_a, along_b, clamp),
                        input);
            results.put(t_closest, point_a, input);
            results.put(t_closest, point_b, input);
            results.put(t_closest, along_a, input);
            results.put(t_closest, along_b, input);
        }
    }
    corpus.section("base/maths vectors end");
}

// mat4.h, quat_rotation.h, rotation_blend.h
void matrix_cases(Corpus& corpus, CorpusDraws draws)
{
    const int count = draws.count;
    corpus.section("base/maths matrices");
    CorpusGroup group(corpus,
                      "(a) base/maths, the matrices and rotations (mat4.h, quat_rotation.h, rotation_blend.h): this "
                      "build vs the 32-bit build");
    Results results(corpus, group);
    Draw draw(corpus, draws.seed);
    CorpusTally& t_axis_angle = results.tally("get_rotation_axis_angle", 'f');
    CorpusTally& t_own_angles = results.tally("turn_in_own_frame(angles)", 'f');
    CorpusTally& t_square = results.tally("square_up_axes", 'f');
    CorpusTally& t_restore = results.tally("restore_right_angles", 'f');
    CorpusTally& t_restored = results.tally("restore_right_angles repaired", 'b');
    CorpusTally& t_local = results.tally("pitch/yaw_locally, roll_in_own_frame", 'f');
    CorpusTally& t_spin = results.tally("Mat4::spin_by", 'f');
    CorpusTally& t_own_axis = results.tally("turn_in_own_frame(axis, angle)", 'f');
    CorpusTally& t_scale = results.tally("*= scale, scale_rows, scale_tool", 'f');
    CorpusTally& t_xform = results.tally("xform", 'f');
    CorpusTally& t_carry = results.tally("carry_point_through", 'f');
    CorpusTally& t_rotate = results.tally("rotate_vector", 'f');
    CorpusTally& t_slide = results.tally("get_slide, offset_local", 'f');
    CorpusTally& t_world = results.tally("spin_around_x, rotate_y, spin_around_z", 'f');
    CorpusTally& t_euler = results.tally("set_from_euler_angles", 'f');
    CorpusTally& t_get_euler = results.tally("get_euler_angles", 'f');
    CorpusTally& t_det = results.tally("get_det", 'd');
    CorpusTally& t_adjugate = results.tally("get_adjugate", 'f');
    CorpusTally& t_upend = results.tally("upend", 'f');
    CorpusTally& t_init = results.tally("init_from_angles", 'f');
    CorpusTally& t_orthonormal = results.tally("invert_orthonormal", 'f');
    CorpusTally& t_axis_matrix = results.tally("build_axis_angle_matrix(axis, angle)", 'f');
    CorpusTally& t_axis_index = results.tally("build_axis_angle_matrix(AxisAngle)", 'f');
    CorpusTally& t_in_plane = results.tally("get_angle_in_plane", 'f');
    CorpusTally& t_look = results.tally("create_look_at_matrix", 'f');
    CorpusTally& t_ortho = results.tally("create_orthographic_matrix", 'f');
    CorpusTally& t_perspective = results.tally("create_perspective_matrix", 'f');
    CorpusTally& t_times = results.tally("Vec4 * Mat4", 'f');
    CorpusTally& t_get_multiply = results.tally("get_multiply, = MatrixProduct", 'f');
    CorpusTally& t_multiply = results.tally("matrix_multiply (Mat4)", 'f');
    CorpusTally& t_make = results.tally("make_pitch/yaw/roll_matrix", 'f');
    CorpusTally& t_quat_axis = results.tally("QuatRotation::init_from_axis_angle", 'f');
    CorpusTally& t_quat_matrix = results.tally("QuatRotation::from_matrix", 'f');
    CorpusTally& t_quat_times = results.tally("QuatRotation *=", 'f');
    CorpusTally& t_quat_spin = results.tally("QuatRotation::spin_by", 'f');
    CorpusTally& t_quat_unit = results.tally("QuatRotation::make_unit", 'f');
    CorpusTally& t_quat_fetch = results.tally("QuatRotation::fetch_mat", 'f');
    CorpusTally& t_blend_small = results.tally("ArcRotationBlender::set_matrices", 'u');
    CorpusTally& t_blend_angle = results.tally("ArcRotationBlender::angle", 'f');
    CorpusTally& t_blend = results.tally("ArcRotationBlender::fetch_mat", 'f');
    for (int i = 0; i < count; ++i) {
        const Mat4 placement = draw.placement();
        const Mat4 second = draw.placement();
        const Mat4 general = draw.general();
        const double input = core::at(placement.row, 0).x;
        Vec4 axis{};
        float angle = 0.0F;
        placement.get_rotation_axis_angle(axis, angle);
        results.put(t_axis_angle, axis, input);
        results.put(t_axis_angle, angle, input);
        const Vec4 angles{.x = draw.angle(), .y = draw.angle(), .z = draw.angle(), .w = 1.0F};
        Mat4 turned = placement;
        turned.turn_in_own_frame(angles);
        results.put(t_own_angles, turned, input);
        Mat4 skewed = placement;
        core::at(skewed.row, 1) += draw.vector() * kSkew;
        core::at(skewed.row, 2) += draw.vector() * kSkew;
        const int square_axis = draw.integer(0, 2);
        Mat4 squared = skewed;
        results.put(t_square, squared.square_up_axes(square_axis), input);
        Mat4 restored = skewed;
        const int short_row = draw.integer(0, 2);
        core::at(restored.row, short_row) *= draw.within(0.0F, 1.0F);
        results.put(t_restored, restored.restore_right_angles(), input);
        results.put(t_restore, restored, input);
        const float turn = draw.angle();
        Mat4 local = placement;
        local.pitch_locally(turn);
        local.yaw_locally(angles.x);
        local.roll_in_own_frame(angles.y);
        results.put(t_local, local, turn);
        const Vec4 direction = draw.direction();
        Mat4 spun = placement;
        results.put(t_spin, spun.spin_by(direction, turn), turn);
        Mat4 own = placement;
        results.put(t_own_axis, own.turn_in_own_frame(direction, turn), turn);
        Mat4 scaled = general;
        scaled *= draw.unit();
        const Vec4 scale = draw.vector();
        scaled.scale_rows(scale);
        scaled.scale_tool(draw.vector());
        results.put(t_scale, scaled, input);
        const Vec4 point = draw.point();
        Vec4 out{};
        results.put(t_xform, general.xform(out, point), input);
        results.put(t_carry, general.carry_point_through(out, point), input);
        results.put(t_rotate, placement.rotate_vector(out, point), input);
        Mat4 slid = placement;
        slid.get_slide(point);
        slid.offset_local(draw.point());
        results.put(t_slide, slid, input);
        Mat4 world = placement;
        world.spin_around_x(angles.x);
        world.rotate_y(angles.y);
        world.spin_around_z(angles.z);
        results.put(t_world, world, input);
        Mat4 euler{};
        euler.set_from_euler_angles(angles.x, angles.y, angles.z);
        results.put(t_euler, euler, angles.x);
        results.put(t_get_euler, placement.get_euler_angles(out), input);
        results.put(t_get_euler, euler.get_euler_angles(out), input);
        results.put(t_det, general.get_det(), input);
        results.put(t_det, placement.get_det(), input);
        Mat4 adjugate{};
        results.put(t_adjugate, adjugate.get_adjugate(general), input);
        Mat4 upended = general;
        results.put(t_upend, upended.upend(), input);
        upended = placement;
        results.put(t_upend, upended.upend(), input);
        Mat4 initial{};
        results.put(t_init, initial.init_from_angles(angles), input);
        Mat4 inverse = placement;
        results.put(t_orthonormal, inverse.invert_orthonormal(), input);
        Mat4 axis_matrix{};
        results.put(t_axis_matrix, maths::build_axis_angle_matrix(axis_matrix, direction, turn), turn);
        results.put(t_axis_index,
                    maths::build_axis_angle_matrix(axis_matrix, {.axis = draw.integer(0, 2), .angle = turn}), turn);
        const int ref_axis = draw.integer(0, 2);
        const int normal_axis = (ref_axis + draw.integer(1, 2)) % kAxes;
        results.put(t_in_plane, maths::get_angle_in_plane(placement, point, ref_axis, normal_axis), input);
        Mat4 view{};
        const Vec4 up{.x = draw.within(-0.2F, 0.2F), .y = 1.0F, .z = draw.within(-0.2F, 0.2F), .w = 0.0F};
        results.put(t_look, maths::create_look_at_matrix(view, point, draw.point(), up), input);
        const float width = draw.within(kSizeMin, kSizeMax);
        const float height = draw.within(kSizeMin, kSizeMax);
        const float near_depth = draw.within(0.1F, 20.0F);
        const float far_depth = near_depth + draw.within(1.0F, 100000.0F);
        results.put(t_ortho,
                    maths::create_orthographic_matrix(
                        view, {.width = width, .height = height, .near_depth_ = near_depth, .far_depth_ = far_depth}),
                    width);
        const float fov = draw.within(0.1F, 3.0F);
        results.put(
            t_perspective,
            maths::create_perspective_matrix(
                view, {.fov = fov, .aspect = width / height, .near_depth_ = near_depth, .far_depth_ = far_depth}),
            fov);
        results.put(t_times, point * general, input);
        Mat4 product{};
        results.put(t_get_multiply, maths::get_multiply(product, placement, general), input);
        product = maths::MatrixProduct{.left = &general, .right = &second};
        results.put(t_get_multiply, product, input);
        maths::matrix_multiply(&product, &placement, &second);
        results.put(t_multiply, product, input);
        Mat4 made{};
        results.put(t_make, maths::make_pitch_matrix(made, turn), turn);
        results.put(t_make, maths::make_yaw_matrix(made, turn), turn);
        results.put(t_make, maths::make_roll_matrix(made, turn), turn);

        maths::QuatRotation quat{};
        quat.init_from_axis_angle(direction, turn);
        results.put(t_quat_axis, maths::vec4_of(quat), turn);
        maths::QuatRotation from{};
        from.from_matrix(placement);
        results.put(t_quat_matrix, maths::vec4_of(from), input);
        maths::QuatRotation times = quat;
        times *= from;
        results.put(t_quat_times, maths::vec4_of(times), input);
        results.put(t_quat_spin, *quat.spin_by(&out, point), input);
        maths::QuatRotation unit = maths::quat_of(draw.vector());
        unit.make_unit(draw.within(0.0F, kQuatLengthMax));
        results.put(t_quat_unit, maths::vec4_of(unit), input);
        Mat4 fetched{};
        results.put(t_quat_fetch, *maths::quat_of(draw.vector()).fetch_mat(&fetched), input);

        // a small rotation half the time (the blender's other path)
        Mat4 target = second;
        if (i % 2 == 0) {
            target = placement;
            target.rotate_y(draw.within(-kSmallTurn, kSmallTurn));
            core::at(target.row, 3) = draw.point();
        }
        maths::ArcRotationBlender blender;
        results.put(t_blend_small, static_cast<int32_t>(blender.set_matrices(placement, target)), input);
        results.put(t_blend_angle, blender.angle(), input);
        const float blend = draw.within(-0.1F, 1.1F);
        Mat4 blended{};
        blender.fetch_mat(blended, blend);
        results.put(t_blend, blended, blend);
        blender.flip_heading();
        blender.fetch_mat(blended, blend);
        results.put(t_blend, blended, blend);
    }
    corpus.section("base/maths matrices end");
}

// shape_tests.h
void shape_cases(Corpus& corpus, CorpusDraws draws)
{
    const int count = draws.count;
    corpus.section("base/maths shapes");
    CorpusGroup group(corpus, "(a) base/maths, the shape tests (shape_tests.h): this build vs the 32-bit build");
    Results results(corpus, group);
    Draw draw(corpus, draws.seed);
    CorpusTally& t_box = results.tally("BBox::append_spot", 'f');
    CorpusTally& t_box_tests = results.tally("BBox intersects, get_holds, point_in_box", 'b');
    CorpusTally& t_center = results.tally("BBox::get_center", 'f');
    CorpusTally& t_contains = results.tally("BBox::contains_point, count_axes_containing", 'u');
    CorpusTally& t_crossing = results.tally("BBox::segment_crossing", 'f');
    CorpusTally& t_crossed = results.tally("BBox::segment_crossing crossed", 'b');
    CorpusTally& t_surface = results.tally("BBox::get_closest_surface_point", 'f');
    CorpusTally& t_loudness = results.tally("Capsule::get_loudness", 'f');
    CorpusTally& t_capsule = results.tally("Capsule::contains_point", 'f');
    CorpusTally& t_capsule_in = results.tally("Capsule::contains_point inside", 'b');
    CorpusTally& t_capsule_segment = results.tally("Capsule::intersects_segment", 'f');
    CorpusTally& t_capsule_hit = results.tally("Capsule::intersects_segment hit", 'u');
    for (int i = 0; i < count; ++i) {
        maths::BBox box(draw.point());
        box.append_spot(draw.point());
        maths::BBox other;
        other.append_spot(draw.point());
        other.append_spot(draw.point());
        const double input = box.min.x;
        results.put(t_box, box.min, input);
        results.put(t_box, box.max, input);
        results.put(t_box_tests, box.intersects(other), input);
        results.put(t_box_tests, box.get_holds(other), input);
        Vec4 center{};
        results.put(t_center, box.get_center(center), input);
        const Vec4 point = draw.point();
        results.put(t_box_tests, maths::point_in_box(&point, &box.max, &box.min), input);
        results.put(t_contains, static_cast<int32_t>(box.contains_point(point)), input);
        uint32_t axes = 0;
        results.put(t_contains, static_cast<int32_t>(box.count_axes_containing(point, axes)), input);
        results.put(t_contains, axes, input);
        const maths::Seg line(point, i % 2 == 0 ? center : draw.point());
        Vec4 hit{};
        Vec4 normal{};
        results.put(t_crossed, box.segment_crossing(line, {.hit = &hit, .normal = &normal}), input);
        results.put(t_crossing, hit, input);
        results.put(t_crossing, normal, input);
        Vec4 surface{};
        box.get_closest_surface_point(point, surface);
        results.put(t_surface, surface, input);
        const Vec4 capsule_start = draw.point();
        const Vec4 capsule_offset = draw.vector() * 200.0F;
        const maths::Capsule capsule(capsule_start, capsule_offset, draw.within(1.0F, 500.0F));
        results.put(t_loudness, capsule.get_loudness(), input);
        Vec4 offset{};
        results.put(t_capsule_in, capsule.contains_point(point, &offset), input);
        results.put(t_capsule, offset, input);
        float along_s = 0.0F;
        float along_t = 0.0F;
        float gap = 0.0F;
        results.put(t_capsule_hit,
                    static_cast<int32_t>(
                        capsule.intersects_segment(line, {.s = &along_s, .t = &along_t, .squared_gap_ = &gap})),
                    input);
        results.put(t_capsule_segment, along_s, input);
        results.put(t_capsule_segment, along_t, input);
        results.put(t_capsule_segment, gap, input);
    }
    corpus.section("base/maths shapes end");
}

// The C runtime's maths as the game calls it (std:: in <cmath>), its double functions and its float ones: on 32-bit
// x86 the float ones are the double ones rounded to float (corecrt_math.h's inline sinf...); the 64-bit runtime has
// float routines of their own.
void runtime_cases(Corpus& corpus, CorpusDraws draws)
{
    const int count = draws.count;
    corpus.section("C runtime");
    CorpusGroup group(corpus, "(a) the C runtime's maths (<cmath>): this build vs the 32-bit build");
    Results results(corpus, group);
    Draw draw(corpus, draws.seed);
    CorpusTally& t_pow = results.tally("pow", 'd');
    CorpusTally& t_exp = results.tally("exp", 'd');
    CorpusTally& t_fmod = results.tally("fmod", 'd');
    CorpusTally& t_hypot = results.tally("hypot", 'd');
    CorpusTally& t_rounding = results.tally("floor, ceil, round, trunc", 'd');
    CorpusTally& t_lround = results.tally("lround, lrint", 'u');
    CorpusTally& t_sin_f = results.tally("sin(float)", 'f');
    CorpusTally& t_cos_f = results.tally("cos(float)", 'f');
    CorpusTally& t_tan_f = results.tally("tan(float)", 'f');
    CorpusTally& t_asin_f = results.tally("asin(float)", 'f');
    CorpusTally& t_acos_f = results.tally("acos(float)", 'f');
    CorpusTally& t_atan_f = results.tally("atan(float)", 'f');
    CorpusTally& t_atan2_f = results.tally("atan2(float, float)", 'f');
    CorpusTally& t_sqrt_f = results.tally("sqrt(float)", 'f');
    CorpusTally& t_pow_f = results.tally("pow(float, float)", 'f');
    CorpusTally& t_exp_f = results.tally("exp(float)", 'f');
    CorpusTally& t_log_f = results.tally("log(float), log10(float)", 'f');
    CorpusTally& t_fmod_f = results.tally("fmod(float, float)", 'f');
    for (int i = 0; i < count; ++i) {
        const float angle = draw.angle();
        const float u = draw.unit();
        const float coordinate = draw.coordinate();
        const float other = draw.coordinate();
        const float positive = draw.positive();
        const float exponent = draw.within(-4.0F, 4.0F);
        results.put(t_pow, std::pow(static_cast<double>(positive), static_cast<double>(exponent)), positive);
        results.put(t_exp, std::exp(static_cast<double>(angle)), angle);
        results.put(t_fmod, std::fmod(static_cast<double>(coordinate), static_cast<double>(other)), coordinate);
        results.put(t_hypot, std::hypot(static_cast<double>(coordinate), static_cast<double>(other)), coordinate);
        const double scaled = static_cast<double>(coordinate) * u;
        results.put(t_rounding, std::floor(scaled), scaled);
        results.put(t_rounding, std::ceil(scaled), scaled);
        results.put(t_rounding, std::round(scaled), scaled);
        results.put(t_rounding, std::trunc(scaled), scaled);
        results.put(t_lround, static_cast<int32_t>(std::lround(scaled)), scaled);
        results.put(t_lround, static_cast<int32_t>(std::lrint(scaled)), scaled);
        results.put(t_sin_f, std::sin(angle), angle);
        results.put(t_cos_f, std::cos(angle), angle);
        results.put(t_tan_f, std::tan(angle), angle);
        results.put(t_asin_f, std::asin(u), u);
        results.put(t_acos_f, std::acos(u), u);
        results.put(t_atan_f, std::atan(coordinate), coordinate);
        results.put(t_atan2_f, std::atan2(coordinate, other), coordinate);
        results.put(t_sqrt_f, std::sqrt(positive), positive);
        results.put(t_pow_f, std::pow(positive, exponent), positive);
        results.put(t_exp_f, std::exp(angle), angle);
        results.put(t_log_f, std::log(positive), positive);
        results.put(t_log_f, std::log10(positive), positive);
        results.put(t_fmod_f, std::fmod(coordinate, other), coordinate);
    }
    corpus.section("C runtime end");
}

} // namespace

void corpus_base_maths(Corpus& corpus, CorpusDraws draws)
{
    // (each case its own sequence)
    runtime_cases(corpus, {.count = draws.count, .seed = draws.seed + 4});
    scalar_cases(corpus, draws);
    vector_cases(corpus, {.count = draws.count, .seed = draws.seed + 1});
    matrix_cases(corpus, {.count = draws.count, .seed = draws.seed + 2});
    shape_cases(corpus, {.count = draws.count, .seed = draws.seed + 3});
}

// view_transforms.h and matrix_multiply (ours) and D3DX9's functions they replaced, on the same inputs: matrices of the
// game's scales (affine ones and projective ones with a positive w), coordinates, unit vectors and tiny ones.
void corpus_d3dx(Corpus& corpus, CorpusDraws draws)
{
    const int count = draws.count;
    using Matrix = std::array<float, maths::kMatrixFloats>;
    using Vector3 = std::array<float, 3>;
    using Vector4 = std::array<float, 4>;
    constexpr std::array<int, 3> kLastColumn = {{3, 7, 11}};
    constexpr int kTranslationFirst = 12;
    constexpr int kTranslationEnd = 15;
    corpus.section("view maths");
    // (printed as they end, the last made first: (a) before (b))
    CorpusGroup d3dx_group(corpus, "(b) D3DX9 (d3dx9_43.dll): this build's DLL vs the 32-bit DLL");
    CorpusGroup ours_group(
        corpus, "(a) base/maths, view_transforms.h and matrix_multiply (our C++): this build vs the 32-bit build");
    Results ours(corpus, ours_group);
    Results d3dx(corpus, d3dx_group);
    Draw draw(corpus, draws.seed);
    struct Pair {
        CorpusTally* ours;
        CorpusTally* d3dx;
    };
    auto pair = [&](const char* name) { return Pair{.ours = &ours.tally(name, 'f'), .d3dx = &d3dx.tally(name, 'f')}; };
    // (our result and D3DX's)
    struct BothResults {
        std::span<const float> ours;
        std::span<const float> d3dx;
    };
    const Pair t_multiply = pair("matrix_multiply");
    const Pair t_point = pair("vec3_transform");
    const Pair t_coord = pair("vec3_transform_coord");
    const Pair t_normal = pair("vec3_transform_normal");
    const Pair t_vec4 = pair("vec4_transform");
    const Pair t_normalize = pair("vec3_normalize");
    const Pair t_transpose = pair("matrix_transpose");
    const Pair t_scaling = pair("matrix_scaling");
    const Pair t_rotation = pair("matrix_rotation_y");
    const Pair t_inverse = pair("matrix_inverse");
    const Pair t_determinant = pair("matrix_inverse determinant");
    CorpusTally& t_invertible_ours = ours.tally("matrix_inverse invertible", 'b');
    CorpusTally& t_invertible_d3dx = d3dx.tally("matrix_inverse invertible", 'b');
    const Pair t_look = pair("matrix_look_at_rh");
    const Pair t_perspective = pair("matrix_perspective_rh");
    const Pair t_ortho = pair("matrix_ortho_rh");
    CorpusTally& t_rsqrt = ours.tally("estimate_reciprocal_sqrt", 'f');
    CorpusTally& t_rcp = ours.tally("estimate_reciprocal", 'f');
    auto put = [&](const Pair& tally, BothResults results, double input) {
        corpus.outs<float>(*tally.ours, results.ours, input);
        corpus.outs<float>(*tally.d3dx, results.d3dx, input);
    };
    for (int index = 0; index < count; ++index) {
        Matrix matrix{};
        for (int k = 0; k < maths::kMatrixFloats; ++k) {
            core::at(matrix, k) = (k >= kTranslationFirst && k < kTranslationEnd) ? draw.coordinate() : draw.unit();
        }
        if (index % 2 == 0) {
            for (const int slot : kLastColumn) {
                core::at(matrix, slot) = 0.0F;
            }
            core::at(matrix, kTranslationEnd) = 1.0F;
        } else {
            core::at(matrix, kTranslationEnd) = draw.within(kSizeMin, kSizeMax);
        }
        const int scale = index % 3; // coordinates, unit vectors, tiny ones
        auto drawn = [&]() {
            const float value = scale == 0 ? draw.coordinate() : draw.unit();
            return scale == 2 ? value * kTinyScale : value;
        };
        const Vector3 v = {{drawn(), drawn(), drawn()}};
        const Vector4 v4 = {{drawn(), drawn(), drawn(), draw.unit()}};
        const double input = core::at(v, 0);
        Matrix right{};
        for (float& value : right) {
            value = draw.unit();
        }
        Matrix fresh{};
        Matrix want{};
        maths::matrix_multiply(fresh, matrix, right);
        tests::d3dx_matrix_multiply(want.data(), matrix.data(), right.data());
        put(t_multiply, {.ours = fresh, .d3dx = want}, input);
        Vector4 fresh4{};
        Vector4 want4{};
        Vector3 fresh3{};
        Vector3 want3{};
        maths::transform_point(fresh4, v, matrix);
        tests::d3dx_vec3_transform(want4, v, matrix);
        put(t_point, {.ours = fresh4, .d3dx = want4}, input);
        maths::transform_coord(fresh3, v, matrix);
        tests::d3dx_vec3_transform_coord(want3, v, matrix);
        put(t_coord, {.ours = fresh3, .d3dx = want3}, input);
        maths::transform_normal(fresh3, v, matrix);
        tests::d3dx_vec3_transform_normal(want3, v, matrix);
        put(t_normal, {.ours = fresh3, .d3dx = want3}, input);
        maths::transform_vec4(fresh4, v4, matrix);
        tests::d3dx_vec4_transform(want4, v4, matrix);
        put(t_vec4, {.ours = fresh4, .d3dx = want4}, input);
        maths::normalize3(fresh3, v);
        tests::d3dx_vec3_normalize(want3, v);
        put(t_normalize, {.ours = fresh3, .d3dx = want3}, input);
        maths::matrix_transpose(fresh, matrix);
        tests::d3dx_matrix_transpose(want, matrix);
        put(t_transpose, {.ours = fresh, .d3dx = want}, input);
        maths::matrix_scaling(fresh, core::at(v, 0), core::at(v, 1), core::at(v, 2));
        tests::d3dx_matrix_scaling(want, core::at(v, 0), core::at(v, 1), core::at(v, 2));
        put(t_scaling, {.ours = fresh, .d3dx = want}, input);
        const float turn = draw.angle();
        maths::matrix_rotation_y(fresh, turn);
        tests::d3dx_matrix_rotation_y(want, turn);
        put(t_rotation, {.ours = fresh, .d3dx = want}, turn);
        float fresh_determinant = 0.0F;
        float want_determinant = 0.0F;
        fresh = {};
        want = {};
        const bool fresh_made = maths::matrix_inverse(fresh, &fresh_determinant, matrix);
        const bool want_made = tests::d3dx_matrix_inverse(want, &want_determinant, matrix);
        corpus.out(t_invertible_ours, fresh_made, input);
        corpus.out(t_invertible_d3dx, want_made, input);
        put(t_determinant,
            {.ours = std::span<const float>(&fresh_determinant, 1),
             .d3dx = std::span<const float>(&want_determinant, 1)},
            input);
        put(t_inverse, {.ours = fresh, .d3dx = want}, input);
        const Vector3 eye = {{draw.coordinate(), draw.coordinate(), draw.coordinate()}};
        const Vector3 at = {{draw.coordinate(), draw.coordinate(), draw.coordinate()}};
        const Vector3 up = index % 2 == 0 ? Vector3{{0.0F, 1.0F, 0.0F}}
                                          : Vector3{{draw.within(-0.2F, 0.2F), 1.0F, draw.within(-0.2F, 0.2F)}};
        maths::matrix_look_at_rh(fresh, eye, at, up);
        tests::d3dx_matrix_look_at_rh(want, {.eye = eye, .at = at, .up = up});
        put(t_look, {.ours = fresh, .d3dx = want}, core::at(eye, 0));
        const float width = draw.within(kSizeMin, kSizeMax);
        const float height = draw.within(kSizeMin, kSizeMax);
        const float zn = draw.within(0.1F, 20.0F);
        const float zf = zn + draw.within(1.0F, 100000.0F);
        maths::matrix_perspective_rh(fresh, width, height, zn, zf);
        tests::d3dx_matrix_perspective_rh(want, width, height, zn, zf);
        put(t_perspective, {.ours = fresh, .d3dx = want}, zn);
        const float ortho_zn = draw.within(-100.0F, 20.0F);
        const float ortho_zf = ortho_zn + draw.within(1.0F, 100000.0F);
        maths::matrix_ortho_rh(fresh, width, height, ortho_zn, ortho_zf);
        tests::d3dx_matrix_ortho_rh(want, width, height, ortho_zn, ortho_zf);
        put(t_ortho, {.ours = fresh, .d3dx = want}, ortho_zn);
        const float positive = draw.within(1.0e-6F, 1.0e6F);
        corpus.out(t_rsqrt, maths::estimate_reciprocal_sqrt(positive), positive);
        corpus.out(t_rcp, maths::estimate_reciprocal(positive), positive);
    }
    corpus.section("view maths end");
}

} // namespace tests
