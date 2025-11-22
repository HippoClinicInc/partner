#include "fft_resampler.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Resample a single channel using FFT
std::vector<float> FFTResampler::Resample(const std::vector<float>& input, size_t target_length)
{
    if (input.empty()) {
        return {};
    }
    
    if (input.size() == target_length) {
        return input;
    }
    
    size_t input_length = input.size();
    
    // Find next power of 2 for efficient FFT
    size_t fft_size = NextPowerOf2(std::max(input_length, target_length));
    
    // Convert input to complex and zero-pad
    std::vector<std::complex<double>> fft_data(fft_size, std::complex<double>(0.0, 0.0));
    for (size_t i = 0; i < input_length; ++i) {
        fft_data[i] = std::complex<double>(input[i], 0.0);
    }
    
    // Forward FFT
    FFT(fft_data, false);
    
    // Prepare output FFT buffer
    std::vector<std::complex<double>> output_fft(fft_size, std::complex<double>(0.0, 0.0));
    
    // Copy frequency components
    // For downsampling: keep low frequencies, discard high frequencies
    // For upsampling: keep all frequencies, zero-pad high frequencies
    size_t copy_length = std::min(fft_size / 2, (target_length * fft_size) / (2 * input_length));
    
    // Copy positive frequencies
    for (size_t i = 0; i <= copy_length; ++i) {
        output_fft[i] = fft_data[i];
    }
    
    // Copy negative frequencies (mirrored)
    for (size_t i = 1; i < copy_length; ++i) {
        output_fft[fft_size - i] = fft_data[fft_size - i];
    }
    
    // Adjust amplitude for length change
    double scale = static_cast<double>(target_length) / static_cast<double>(input_length);
    for (auto& val : output_fft) {
        val *= scale;
    }
    
    // Inverse FFT
    FFT(output_fft, true);
    
    // Extract real part and resample to exact target length
    std::vector<float> output;
    output.reserve(target_length);
    
    double step = static_cast<double>(fft_size) / static_cast<double>(target_length);
    for (size_t i = 0; i < target_length; ++i) {
        size_t idx = static_cast<size_t>(i * step);
        if (idx >= fft_size) idx = fft_size - 1;
        output.push_back(static_cast<float>(output_fft[idx].real()));
    }
    
    return output;
}

// Resample multiple channels
std::vector<std::vector<float>> FFTResampler::ResampleMultiChannel(
    const std::vector<std::vector<float>>& input_data,
    int down_sample_rate)
{
    if (down_sample_rate <= 0) {
        throw std::invalid_argument("down_sample_rate must be positive");
    }
    
    if (down_sample_rate == 1) {
        return input_data;
    }
    
    std::vector<std::vector<float>> resampled_data;
    resampled_data.reserve(input_data.size());
    
    for (const auto& channel_data : input_data) {
        if (channel_data.empty()) {
            resampled_data.push_back({});
            continue;
        }
        
        size_t target_length = channel_data.size() / down_sample_rate;
        if (target_length == 0) {
            target_length = 1;
        }
        
        auto resampled_channel = Resample(channel_data, target_length);
        resampled_data.push_back(resampled_channel);
    }
    
    return resampled_data;
}

// Cooley-Tukey FFT algorithm (radix-2, in-place)
void FFTResampler::FFT(std::vector<std::complex<double>>& data, bool inverse)
{
    size_t n = data.size();
    
    if (n <= 1) {
        return;
    }
    
    // Bit-reversal permutation
    BitReversalPermutation(data);
    
    // Cooley-Tukey decimation-in-time radix-2 FFT
    for (size_t s = 1; s <= static_cast<size_t>(log2(n)); ++s) {
        size_t m = 1 << s;  // 2^s
        size_t m2 = m >> 1; // m/2
        
        // Compute twiddle factor
        double angle = (inverse ? 2.0 : -2.0) * M_PI / m;
        std::complex<double> wm(cos(angle), sin(angle));
        
        for (size_t k = 0; k < n; k += m) {
            std::complex<double> w(1.0, 0.0);
            
            for (size_t j = 0; j < m2; ++j) {
                std::complex<double> t = w * data[k + j + m2];
                std::complex<double> u = data[k + j];
                
                data[k + j] = u + t;
                data[k + j + m2] = u - t;
                
                w *= wm;
            }
        }
    }
    
    // Normalize for inverse FFT
    if (inverse) {
        double scale = 1.0 / n;
        for (auto& val : data) {
            val *= scale;
        }
    }
}

// Bit-reversal permutation
void FFTResampler::BitReversalPermutation(std::vector<std::complex<double>>& data)
{
    size_t n = data.size();
    size_t bits = static_cast<size_t>(log2(n));
    
    for (size_t i = 0; i < n; ++i) {
        size_t j = 0;
        for (size_t b = 0; b < bits; ++b) {
            j = (j << 1) | ((i >> b) & 1);
        }
        
        if (j > i) {
            std::swap(data[i], data[j]);
        }
    }
}

// Find next power of 2
size_t FFTResampler::NextPowerOf2(size_t n)
{
    if (n == 0) {
        return 1;
    }
    
    // Check if already power of 2
    if ((n & (n - 1)) == 0) {
        return n;
    }
    
    // Find next power of 2
    size_t power = 1;
    while (power < n) {
        power <<= 1;
    }
    
    return power;
}
