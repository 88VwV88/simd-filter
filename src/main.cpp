#include "lodepng.h"
#define FILTERS_IMPLEMENTATION
#include "filters.hpp"

#include <boost/program_options.hpp>
#include <iostream>
#include <print>

namespace po = boost::program_options;

enum Image_Filter {
  GREYSCALE,
  INVERT,
  GAUSSIAN,
  LAPLACE,
};

Image_Filter filter_to_image_filter(std::string const &filter) {
  if (filter == "greyscale")
    return Image_Filter::GREYSCALE;
  else if (filter == "invert")
    return Image_Filter::INVERT;
  else if (filter == "gaussian")
    return Image_Filter::GAUSSIAN;
  else if (filter == "laplace")
    return Image_Filter::LAPLACE;
  else
    throw std::invalid_argument("Invalid image filter");
}

LodePNGColorType format_to_color_type(std::string const &format) {
  if (format == "rgb")
    return LodePNGColorType::LCT_RGB;
  else if (format == "alpha")
    return LodePNGColorType::LCT_GREY_ALPHA;
  else if (format == "grey")
    return LodePNGColorType::LCT_GREY;
  else
    throw std::invalid_argument("Invalid image format");
}

std::tuple<unsigned int, unsigned int, std::vector<unsigned char>>
get_image_bytes(std::string const &filename, std::string const &format) {
  unsigned int width, height;
  std::vector<unsigned char> bytes;
  lodepng::decode(bytes, width, height, filename, format_to_color_type(format));
  return std::make_tuple(width, height, bytes);
}

void write_image_bytes(std::vector<unsigned char> const &bytes,
                       unsigned int width, unsigned int height,
                       std::string const &filename, std::string const &format) {
  std::vector<unsigned char> encoded;
  auto error = lodepng::encode(encoded, bytes, width, height,
                               format_to_color_type(format));
  if (error)
    throw std::runtime_error(std::string{"Error encoding PNG file: "} +
                             lodepng_error_text(error));
  lodepng::save_file(encoded, filename);
}

int main(int argc, char *argv[]) {
  unsigned int blur_strength;
  std::string input_file, output_file;
  std::string filter;

  po::options_description desc("Allowed options");

  // clang-format off
  desc.add_options()
    ("help,h", "Produce this help message")
    ("filter,F", po::value<std::string>(&filter)->default_value("greyscale"), "Set the image filter")
    ("input-file,I", po::value<std::string>(&input_file), "Set the input filename")
    ("output-file,O", po::value<std::string>(&output_file), "Set the output filename")
    ("blur-strength", po::value<unsigned int>(&blur_strength)->default_value(10), "Set the gaussian blur strength");
  // clang-format on

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  if (vm.count("help")) {
    std::cout << desc << std::endl;
    return EXIT_SUCCESS;
  }

  if (!vm.count("input-file")) {
    std::println(std::cerr, "Missing required option: input-file");
    std::cerr << desc << std::endl;
    return EXIT_FAILURE;
  }

  if (!vm.count("output-file"))
    output_file = "out-" + input_file;

  auto [width, height, bytes] = get_image_bytes(input_file, "rgb");

  std::vector<unsigned char> filtered;

  std::string output_format;
  if (filter == "greyscale") {
    filtered = apply_greyscale_rgb_simd(bytes);
    output_format = "grey";
  } else if (filter == "invert") {
    filtered = apply_invert_rgb_simd(bytes);
    output_format = "rgb";
  } else if (filter == "gaussian") {
    filtered = apply_gaussian_rgb(bytes, width, height, blur_strength);
    output_format = "rgb";
  } else if (filter == "laplace") {
    filtered = apply_laplacian_rgb(bytes, width, height);
    output_format = "grey";
  } else {
    filtered = bytes;
  }

  write_image_bytes(filtered, width, height, output_file, output_format);
}