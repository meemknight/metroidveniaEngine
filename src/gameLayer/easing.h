#pragma once

#include <algorithm>
#include <cmath>

namespace easing
{
	inline float clamp01(float value)
	{
		return std::clamp(value, 0.f, 1.f);
	}

	// A quick ease-out works well for dash motion because it snaps forward fast
	// and then settles into the target instead of feeling linear and robotic.
	inline float easeOutCubic(float value)
	{
		value = clamp01(value);
		float inv = 1.f - value;
		return 1.f - inv * inv * inv;
	}

	// Exponential damping is handy for tiny movement easing because it feels smooth
	// and predictable without introducing a second velocity system.
	inline float damp(float current, float target, float smoothTime, float deltaTime)
	{
		smoothTime = std::max(smoothTime, 0.0001f);
		float blend = 1.f - std::exp(-deltaTime / smoothTime);
		return current + (target - current) * blend;
	}
}
