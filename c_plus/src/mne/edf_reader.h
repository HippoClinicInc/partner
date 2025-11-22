#ifndef EDF_READER_H
#define EDF_READER_H

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include "edflib.h"

/**
 * @brief EDF data container class, similar to MNE's Raw object
 *
 * Stores all channel data and metadata read from EDF/BDF files
 */
class EDFRaw
{
public:
    // Channel data: each channel is a double vector
    std::vector<std::vector<double>> data; // [n_channels][n_samples]

    // Channel metadata
    std::vector<std::string> ch_names; // Channel names
    std::vector<double> sfreq; // Sampling rate for each channel
    std::vector<std::string> ch_types; // Channel types (e.g., "eeg", "eog", etc.)
    std::vector<std::string> units; // Physical units (e.g., "uV", "mV", etc.)

    // File metadata
    std::string filepath; // File path
    int filetype; // File type (EDF, EDF+, BDF, BDF+)
    double duration; // File duration (seconds)
    int n_channels; // Number of channels
    int n_samples; // Number of samples per channel

    // Time information
    int startdate_year, startdate_month, startdate_day;
    int starttime_hour, starttime_minute, starttime_second;
    long long starttime_subsecond; // Sub-second time (100 nanosecond units)

    // Patient information
    std::string patient_name;
    std::string patient_code;
    std::string sex;
    std::string birthdate;
    int birthdate_year; // Birth year
    int birthdate_month; // Birth month
    int birthdate_day; // Birth day

    // Annotations/events
    std::vector<edflib_annotation_t> annotations;

    /**
     * @brief Get data for the specified channel
     * @param ch_idx Channel index (0-based)
     * @return Reference to channel data
     */
    const std::vector<double>& get_channel_data(int ch_idx) const
    {
        if (ch_idx < 0 || ch_idx >= n_channels)
        {
            throw std::out_of_range("Channel index out of range");
        }
        return data[ch_idx];
    }

    /**
     * @brief Get sampling rate for the specified channel
     * @param ch_idx Channel index
     * @return Sampling rate (Hz)
     */
    double get_sfreq(int ch_idx = 0) const
    {
        if (ch_idx < 0 || ch_idx >= n_channels)
        {
            throw std::out_of_range("Channel index out of range");
        }
        return sfreq[ch_idx];
    }

    /**
     * @brief Get time axis (seconds)
     * @return Vector of time points
     */
    std::vector<double> get_times() const
    {
        if (n_samples == 0 || n_channels == 0)
        {
            return {};
        }
        double sample_rate = sfreq[0];
        std::vector<double> times(n_samples);
        for (int i = 0; i < n_samples; i++)
        {
            times[i] = i / sample_rate;
        }
        return times;
    }

    /**
     * @brief Print file information summary
     */
    void print_info() const
    {
        std::cout << "=== EDF File Information ===" << std::endl;
        std::cout << "File path: " << filepath << std::endl;
        std::cout << "File type: ";
        switch (filetype)
        {
        case EDFLIB_FILETYPE_EDF:
            std::cout << "EDF";
            break;
        case EDFLIB_FILETYPE_EDFPLUS:
            std::cout << "EDF+";
            break;
        case EDFLIB_FILETYPE_BDF:
            std::cout << "BDF";
            break;
        case EDFLIB_FILETYPE_BDFPLUS:
            std::cout << "BDF+";
            break;
        default:
            std::cout << "Unknown";
            break;
        }
        std::cout << std::endl;
        std::cout << "Number of channels: " << n_channels << std::endl;
        std::cout << "Number of samples: " << n_samples << std::endl;
        std::cout << "Duration: " << duration << " seconds" << std::endl;
        if (n_channels > 0)
        {
            std::cout << "Sampling rate: " << sfreq[0] << " Hz" << std::endl;
        }
        std::cout << "Start time: " << startdate_year << "-" << startdate_month << "-" << startdate_day << " "
                  << starttime_hour << ":" << starttime_minute << ":" << starttime_second << std::endl;
        if (!patient_name.empty())
        {
            std::cout << "Patient name: " << patient_name << std::endl;
        }
        std::cout << "Number of annotations: " << annotations.size() << std::endl;
    }
};

/**
 * @brief Read EDF/BDF file, similar to MNE's read_raw()
 *
 * @param filepath File path
 * @param preload Whether to immediately load all data into memory (true) or lazy load (false)
 *                Current implementation only supports preload=true
 * @param verbose Whether to print detailed information
 * @return std::unique_ptr<EDFRaw> Smart pointer to EDFRaw object, returns nullptr on failure
 */
std::unique_ptr<EDFRaw> read_raw(const std::string& filepath, bool preload = true, bool verbose = false);

#endif // EDF_READER_H
