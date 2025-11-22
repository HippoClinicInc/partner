#ifndef FFT_RESAMPLER_H
#define FFT_RESAMPLER_H

#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>

/**
 * @brief FFT-based resampler using Cooley-Tukey algorithm
 * 
 * This implementation provides high-quality resampling similar to scipy.signal.resample
 * or mne.filter.resample in Python. It uses FFT to resample in the frequency domain.
 */
class FFTResampler
{
public:
    /**
     * @brief Resample a signal using FFT
     * @param input Input signal
     * @param target_length Target length after resampling
     * @return Resampled signal
     */
    static std::vector<float> Resample(const std::vector<float>& input, size_t target_length);

    /**
     * @brief Resample multiple channels
     * @param input_data 2D array [channels][samples]
     * @param down_sample_rate Downsampling factor
     * @return Resampled 2D array
     */
    static std::vector<std::vector<float>> ResampleMultiChannel(
        const std::vector<std::vector<float>>& input_data,
        int down_sample_rate);

private:
    /**
     * @brief Compute FFT using Cooley-Tukey algorithm
     * @param data Complex data (in-place transform)
     * @param inverse If true, compute inverse FFT
     */
    static void FFT(std::vector<std::complex<double>>& data, bool inverse = false);

    /**
     * @brief Bit-reversal permutation for FFT
     * @param data Data to permute
     */
    static void BitReversalPermutation(std::vector<std::complex<double>>& data);

    /**
     * @brief Find next power of 2 greater than or equal to n
     * @param n Input number
     * @return Next power of 2
     */
    static size_t NextPowerOf2(size_t n);
};

#endif // FFT_RESAMPLER_H
