// NOT ORIGINAL - the water's waves from physics (water_waves.h).
#include "engine/gfx/water_waves.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

#include "base/core/checked_index.h"

namespace render::water {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kGamma = 3.3;       // JONSWAP's peak enhancement
constexpr double kSigmaBelow = 0.07; // ... its width below the peak
constexpr double kSigmaAbove = 0.09; // ... above it
constexpr double kPeakShape = 1.25;  // (5 / 4: Pierson-Moskowitz's low-frequency cut)
constexpr double kLeastShare = 1e-3; // (the spectrum's integral from this x the peak ...)
constexpr double kMostShare = 60.0;  // (... to this)
constexpr int kIntegralSteps = 600;
constexpr double kHeightOverSpread = 4.0; // Hs = 4 sqrt(variance)
// Young and Verhagen 1996
constexpr double kEnergyLimit = 3.64e-3;
constexpr double kEnergyDepth = 0.493;
constexpr double kEnergyDepthPower = 0.75;
constexpr double kEnergyFetch = 3.13e-3;
constexpr double kEnergyFetchPower = 0.57;
constexpr double kEnergyPower = 1.74;
constexpr double kPeakLimit = 0.133;
constexpr double kPeakDepth = 0.331;
constexpr double kPeakDepthPower = 1.01;
constexpr double kPeakFetch = 5.215e-4;
constexpr double kPeakFetchPower = 0.73;
constexpr double kPeakPower = -0.37;
// JONSWAP's Phillips constant 0.076 chi^-0.22, held within Pierson-Moskowitz's 0.0081 and its value at chi 10 (the
// fetches it was measured over begin there)
constexpr double kPhillips = 0.076;
constexpr double kPhillipsPower = -0.22;
constexpr double kPhillipsLeast = 0.0081;
constexpr double kPhillipsLeastFetch = 10.0;
// Monahan and O'Muircheartaigh 1980: whitecap coverage 3.84e-6 U^3.41
constexpr double kWhitecapScale = 3.84e-6;
constexpr double kWhitecapPower = 3.41;
constexpr double kWhitecapFull = 0.01; // the crests' foam at full strength from this share white

// Young and Verhagen's form: [tanh A tanh(B / tanh A)]
double growth(double a, double b)
{
    const double t = std::tanh(a);
    return t > 0.0 ? t * std::tanh(b / t) : 0.0;
}

} // namespace

double omega_of(double k, double depth)
{
    const double kd = std::min(k * std::max(depth, 0.0), 40.0);
    return std::sqrt(std::max(((kGravity * k) + (kSurfaceTension * k * k * k)) * std::tanh(kd), 0.0));
}

double wavenumber_of(double omega, double depth)
{
    if (!(omega > 0.0) || !(depth > 0.0)) {
        return 0.0;
    }
    // (omega_of rises with k: bisected in log k)
    double low = std::log(1e-7);
    double high = std::log(1e6);
    constexpr int kHalvings = 80;
    for (int i = 0; i < kHalvings; ++i) {
        const double middle = 0.5 * (low + high);
        if (omega_of(std::exp(middle), depth) < omega) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return std::exp(0.5 * (low + high));
}

WindSea wind_sea(double wind, double fetch, double depth)
{
    WindSea out;
    out.wind = std::max(wind, 0.0);
    if (!(out.wind >= kOnsetWind) || !(fetch > 0.0) || !(depth > 0.0)) {
        return out;
    }
    const double u2 = out.wind * out.wind;
    const double chi = kGravity * fetch / u2;
    const double delta = kGravity * depth / u2;
    const double energy = kEnergyLimit * std::pow(growth(kEnergyDepth * std::pow(delta, kEnergyDepthPower),
                                                         kEnergyFetch * std::pow(chi, kEnergyFetchPower)),
                                                  kEnergyPower);
    const double peak = kPeakLimit * std::pow(growth(kPeakDepth * std::pow(delta, kPeakDepthPower),
                                                     kPeakFetch * std::pow(chi, kPeakFetchPower)),
                                              kPeakPower);
    const double variance = energy * u2 * u2 / (kGravity * kGravity);
    out.height = std::min(kHeightOverSpread * std::sqrt(variance), kBreaking * depth);
    out.frequency = peak * kGravity / out.wind;
    const double k = wavenumber_of(kTwoPi * out.frequency, depth);
    out.wavelength = k > 0.0 ? kTwoPi / k : 0.0;
    if (!(out.height > 0.0) || !(out.wavelength > 0.0) || !std::isfinite(out.height)) {
        return WindSea{.wind = out.wind};
    }
    return out;
}

double jonswap_shape(double f, double fp)
{
    if (!(f > 0.0) || !(fp > 0.0)) {
        return 0.0;
    }
    const double x = f / fp;
    const double sigma = x <= 1.0 ? kSigmaBelow : kSigmaAbove;
    const double r = std::exp(-((x - 1.0) * (x - 1.0)) / (2.0 * sigma * sigma));
    const double x2 = x * x;
    return std::exp(-kPeakShape / (x2 * x2)) * std::pow(kGamma, r) / (x2 * x2 * x);
}

double jonswap_shape_integral(double f0, double f1, double fp)
{
    if (!(fp > 0.0)) {
        return 0.0;
    }
    const double low = std::max(f0, fp * kLeastShare);
    const double high = std::min(f1, fp * kMostShare);
    if (!(high > low)) {
        return 0.0;
    }
    // (Simpson's rule in log f: the peak and the tail evenly sampled)
    const double a = std::log(low);
    const double b = std::log(high);
    const double h = (b - a) / kIntegralSteps;
    double sum = 0.0;
    for (int i = 0; i <= kIntegralSteps; ++i) {
        const double f = std::exp(a + (h * i));
        const double weight = (i == 0 || i == kIntegralSteps) ? 1.0 : ((i % 2) != 0 ? 4.0 : 2.0);
        sum += weight * jonswap_shape(f, fp) * f;
    }
    return sum * h / 3.0;
}

SeaWaves sea_waves(const WindSea& sea, double fetch, double depth)
{
    SeaWaves out;
    if (!(sea.height > 0.0) || !(sea.wavelength > 0.0) || !(sea.frequency > 0.0)) {
        return out;
    }
    const double total = jonswap_shape_integral(0.0, sea.frequency * kMostShare, sea.frequency);
    if (!(total > 0.0)) {
        return out;
    }
    const double variance = (sea.height / kHeightOverSpread) * (sea.height / kHeightOverSpread);
    std::array<double, 4> f{};
    std::array<double, 4> k{};
    for (std::size_t i = 0; i < kSwellLengths.size(); ++i) {
        core::at(k, i) = kTwoPi / (sea.wavelength * core::at(kSwellLengths, i));
        core::at(f, i) = omega_of(core::at(k, i), depth) / kTwoPi;
    }
    // each wave the energy of its band: the bands' edges halfway (geometrically) between the waves' frequencies, the
    // first from nothing, the last as wide above as below
    std::array<double, 5> edges{};
    for (std::size_t i = 1; i < kSwellLengths.size(); ++i) {
        core::at(edges, i) = std::sqrt(core::at(f, i - 1) * core::at(f, i));
    }
    const double last = core::at(f, 3);
    core::at(edges, 4) = last * (last / core::at(edges, 3));
    for (std::size_t i = 0; i < kSwellLengths.size(); ++i) {
        const double share = jonswap_shape_integral(core::at(edges, i), core::at(edges, i + 1), sea.frequency) / total;
        const double amplitude = std::sqrt(2.0 * variance * share);
        core::at(out.slopes, i) = static_cast<float>(amplitude * core::at(k, i));
        core::at(out.speeds, i) = static_cast<float>(kTwoPi * core::at(f, i) / core::at(k, i) * kUnitsPerMetre);
    }
    out.wavelength = static_cast<float>(sea.wavelength * kUnitsPerMetre);
    out.height = static_cast<float>(sea.height * kUnitsPerMetre);
    // the ripples: the equilibrium tail past the last wave's band, over the texture's band - mean square slope alpha
    // ln(f2 / f1) (deep water's dispersion: k^2 S(f) = alpha / f)
    const double ripple_low = omega_of(kTwoPi / (kRippleLongest / kUnitsPerMetre), depth) / kTwoPi;
    const double ripple_high = omega_of(kTwoPi / (kRippleShortest / kUnitsPerMetre), depth) / kTwoPi;
    const double from = std::max(core::at(edges, 4), ripple_low);
    if (ripple_high > from) {
        const double chi = std::max(kGravity * fetch / (sea.wind * sea.wind), kPhillipsLeastFetch);
        const double alpha = std::max(kPhillips * std::pow(chi, kPhillipsPower), kPhillipsLeast);
        out.ripples = static_cast<float>(std::sqrt(alpha * std::log(ripple_high / from)) / kRippleTextureSlope);
    }
    out.whitecaps = static_cast<float>(
        std::clamp(kWhitecapScale * std::pow(sea.wind, kWhitecapPower) / kWhitecapFull, 0.0, 1.0));
    return out;
}

} // namespace render::water
