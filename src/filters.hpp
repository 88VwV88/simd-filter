#ifndef FILTERS_HPP_
#define FILTERS_HPP_

#include <vector>

/**
 * @brief Converts an RGB image buffer to single-channel greyscale using SIMD.
 *
 * Uses the luminance formula: Y = 0.299*R + 0.587*G + 0.114*B
 * implemented with fixed-point arithmetic for performance.
 *
 * @param bytes Input RGB buffer (3 bytes per pixel).
 * @return std::vector<unsigned char> Greyscale output (1 byte per pixel).
 * @throws std::invalid_argument If buffer size is not a multiple of 3.
 */
std::vector<unsigned char>
apply_greyscale_rgb_simd(const std::vector<unsigned char> &bytes);

/**
 * @brief Inverts RGB image colors using SIMD.
 *
 * Applies the transformation: output = 255 - input for each channel.
 *
 * @param bytes Input RGB buffer (3 bytes per pixel).
 * @return std::vector<unsigned char> Inverted RGB output (same size as input).
 * @throws std::invalid_argument If buffer size is not a multiple of 3.
 */
std::vector<unsigned char>
apply_invert_rgb_simd(const std::vector<unsigned char> &bytes);

/**
 * @brief Retrieves a pixel channel value with boundary clamping.
 *
 * @param src Source image buffer.
 * @param x X coordinate (clamped to [0, width-1]).
 * @param y Y coordinate (clamped to [0, height-1]).
 * @param width Image width.
 * @param height Image height.
 * @param channel Channel index to retrieve.
 * @param channels Total number of channels per pixel.
 * @return unsigned char The pixel channel value.
 */
inline unsigned char get_pixel_clamped(const unsigned char *src, int x, int y,
                                       int width, int height, int channel,
                                       int channels);

/**
 * @brief Generates a normalized 1D Gaussian kernel.
 *
 * The kernel radius is calculated as ceil(3*sigma) to cover 99.7% of the
 * distribution.
 *
 * @param sigma Standard deviation of the Gaussian distribution.
 * @return std::pair<std::vector<double>, int> Pair of (kernel weights, radius).
 */
std::pair<std::vector<double>, int> generate_gaussian_kernel(double sigma);

/**
 * @brief Applies Gaussian blur to an RGB image using separable convolution.
 *
 * Uses a two-pass approach (horizontal then vertical) for O(n*r) complexity
 * instead of O(n*r^2) for a full 2D convolution.
 *
 * @param bytes Input RGB buffer (3 bytes per pixel).
 * @param width Image width in pixels.
 * @param height Image height in pixels.
 * @param blur_strength Blur intensity (sigma = blur_strength / 10.0).
 * @return std::vector<unsigned char> Blurred RGB output (same size as input).
 * @throws std::invalid_argument If buffer size is not a multiple of 3.
 */
std::vector<unsigned char>
apply_gaussian_rgb(const std::vector<unsigned char> &bytes, unsigned int width,
                   unsigned int height, unsigned int blur_strength);

/**
 * @brief Applies Laplacian edge detection to an RGB image.
 *
 * Converts the image to greyscale first, then applies the Laplacian kernel:
 * @code
 *   [ 0 -1  0]
 *   [-1  4 -1]
 *   [ 0 -1  0]
 * @endcode
 *
 * @param bytes Input RGB buffer (3 bytes per pixel).
 * @param width Image width in pixels.
 * @param height Image height in pixels.
 * @return std::vector<unsigned char> Greyscale edge map (1 byte per pixel).
 * @throws std::invalid_argument If buffer size is not a multiple of 3.
 */
std::vector<unsigned char>
apply_laplacian_rgb(const std::vector<unsigned char> &bytes, unsigned int width,
                    unsigned int height);

#endif

#ifdef FILTERS_IMPLEMENTATION

#include <emmintrin.h>
#include <pmmintrin.h>
#include <tmmintrin.h>
#include <xmmintrin.h>

#include "lodepng.h"

#include <boost/align/is_aligned.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

std::vector<unsigned char>
apply_greyscale_rgb_simd(const std::vector<unsigned char> &bytes) {
  if (bytes.size() % 3 != 0)
    throw std::invalid_argument("RGB buffer must have a multiple of 3 bytes");

  const std::size_t pixels = bytes.size() / 3;
  std::vector<unsigned char> output(pixels);

  const unsigned char *src = bytes.data();
  unsigned char *dst = output.data();

  std::size_t i = 0;

  for (; i + 8 <= pixels; i += 8) {
    alignas(16) unsigned char greys[8];
    for (std::size_t j = 0; j < 8; ++j) {
      const std::size_t idx = (i + j) * 3;
      const unsigned char r = src[idx];
      const unsigned char g = src[idx + 1];
      const unsigned char b = src[idx + 2];
      greys[j] =
          static_cast<unsigned char>((77 * r + 150 * g + 29 * b + 128) >> 8);
    }
    _mm_storel_epi64(reinterpret_cast<__m128i *>(dst + i),
                     _mm_loadl_epi64(reinterpret_cast<const __m128i *>(greys)));
  }

  for (; i < pixels; ++i) {
    const std::size_t idx = i * 3;
    const unsigned char r = src[idx];
    const unsigned char g = src[idx + 1];
    const unsigned char b = src[idx + 2];
    dst[i] = static_cast<unsigned char>((77 * r + 150 * g + 29 * b + 128) >> 8);
  }

  return output;
}

std::vector<unsigned char>
apply_invert_rgb_simd(const std::vector<unsigned char> &bytes) {
  if (bytes.size() % 3 != 0)
    throw std::invalid_argument("RGB buffer must have a multiple of 3 bytes");

  std::vector<unsigned char> output(bytes.size());
  const unsigned char *src = bytes.data();
  unsigned char *dst = output.data();
  const std::size_t total = bytes.size();

  const __m128i all_ones = _mm_set1_epi8(static_cast<char>(0xFF));

  std::size_t i = 0;

  for (; i + 16 <= total; i += 16) {
    __m128i pixels =
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + i));
    __m128i inverted = _mm_sub_epi8(all_ones, pixels);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + i), inverted);
  }

  for (; i < total; ++i) {
    dst[i] = 255 - src[i];
  }

  return output;
}

inline unsigned char get_pixel_clamped(const unsigned char *src, int x, int y,
                                       int width, int height, int channel,
                                       int channels) {
  x = std::clamp(x, 0, width - 1);
  y = std::clamp(y, 0, height - 1);
  return src[(y * width + x) * channels + channel];
}

std::pair<std::vector<double>, int> generate_gaussian_kernel(double sigma) {
  int radius = std::max<int>(1, static_cast<int>(std::ceil(sigma * 3.0)));
  int size = 2 * radius + 1;

  std::vector<double> kernel(size);
  double sum = 0.0;

  for (int i = 0; i < size; ++i) {
    int x = i - radius;
    double value = std::exp(-(x * x) / (2.0 * sigma * sigma));
    kernel[i] = value;
    sum += value;
  }

  for (auto &k : kernel) {
    k /= sum;
  }

  return {kernel, radius};
}

std::vector<unsigned char>
apply_gaussian_rgb(const std::vector<unsigned char> &bytes, unsigned int width,
                   unsigned int height, unsigned int blur_strength) {
  if (bytes.size() % 3 != 0)
    throw std::invalid_argument("RGB buffer must have a multiple of 3 bytes");

  const int w = static_cast<int>(width);
  const int h = static_cast<int>(height);
  const int channels = 3;

  double sigma = static_cast<double>(blur_strength) / 10.0;
  if (sigma < 0.1)
    sigma = 0.1;

  auto [kernel, radius] = generate_gaussian_kernel(sigma);

  std::vector<unsigned char> temp(bytes.size());
  std::vector<unsigned char> output(bytes.size());
  const unsigned char *src = bytes.data();

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      for (int c = 0; c < channels; ++c) {
        double sum = 0.0;
        for (int k = -radius; k <= radius; ++k) {
          int sx = std::clamp(x + k, 0, w - 1);
          sum += kernel[k + radius] * src[(y * w + sx) * channels + c];
        }
        temp[(y * w + x) * channels + c] =
            static_cast<unsigned char>(std::clamp(sum, 0.0, 255.0));
      }
    }
  }

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      for (int c = 0; c < channels; ++c) {
        double sum = 0.0;
        for (int k = -radius; k <= radius; ++k) {
          int sy = std::clamp(y + k, 0, h - 1);
          sum += kernel[k + radius] * temp[(sy * w + x) * channels + c];
        }
        output[(y * w + x) * channels + c] =
            static_cast<unsigned char>(std::clamp(sum, 0.0, 255.0));
      }
    }
  }

  return output;
}

std::vector<unsigned char>
apply_laplacian_rgb(const std::vector<unsigned char> &bytes, unsigned int width,
                    unsigned int height) {
  if (bytes.size() % 3 != 0)
    throw std::invalid_argument("RGB buffer must have a multiple of 3 bytes");

  const int w = static_cast<int>(width);
  const int h = static_cast<int>(height);
  const std::size_t pixels = static_cast<std::size_t>(w * h);

  std::vector<unsigned char> grey(pixels);
  const unsigned char *src = bytes.data();

  for (std::size_t i = 0; i < pixels; ++i) {
    const std::size_t idx = i * 3;
    const unsigned char r = src[idx];
    const unsigned char g = src[idx + 1];
    const unsigned char b = src[idx + 2];
    grey[i] =
        static_cast<unsigned char>((77 * r + 150 * g + 29 * b + 128) >> 8);
  }

  std::vector<unsigned char> output(pixels);

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int sum = 0;

      auto get_grey = [&](int px, int py) -> int {
        px = std::clamp(px, 0, w - 1);
        py = std::clamp(py, 0, h - 1);
        return static_cast<int>(grey[py * w + px]);
      };

      sum += -1 * get_grey(x, y - 1);
      sum += -1 * get_grey(x - 1, y);
      sum += 4 * get_grey(x, y);
      sum += -1 * get_grey(x + 1, y);
      sum += -1 * get_grey(x, y + 1);

      sum = std::clamp<int>(std::abs(sum), 0, 255);
      output[y * w + x] = static_cast<unsigned char>(sum);
    }
  }

  return output;
}

#endif