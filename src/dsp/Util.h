#pragma once

// Small pure helpers shared by the DSP classes.

namespace clearspace::dsp
{

/** Exact floating-point comparison, spelled out so -Wfloat-equal stays clean where an exact
    match is the intent (e.g. "is this gain exactly unity, so we can skip the multiply"). */
template <typename T>
constexpr bool exactlyEqual(T a, T b)
{
    return !(a < b) && !(b < a);
}

} // namespace clearspace::dsp
