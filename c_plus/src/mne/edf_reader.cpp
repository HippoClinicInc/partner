#include "edf_reader.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

#ifndef EDFLIB_NO_ERROR
#define EDFLIB_NO_ERROR 0
#endif

std::unique_ptr<EDFRaw> read_raw(const std::string& filepath, bool preload, bool verbose)
{
    if (!preload)
    {
        std::cerr << "Warning: Current implementation does not support preload=false, using preload=true" << std::endl;
    }

    // Create EDFRaw object
    auto raw = std::make_unique<EDFRaw>();
    raw->filepath = filepath;

    // Open file
    edflib_hdr_t hdr;
    int handle = edfopen_file_readonly(filepath.c_str(), &hdr, EDFLIB_READ_ALL_ANNOTATIONS);

    if (handle < 0)
    {
        std::cerr << "Failed to open EDF file: " << filepath << std::endl;
        std::cerr << "Error code: " << handle << std::endl;
        return nullptr;
    }

    // Copy file metadata
    raw->filetype = hdr.filetype;
    raw->n_channels = hdr.edfsignals;
    raw->duration = static_cast<double>(hdr.file_duration) / EDFLIB_TIME_DIMENSION;
    raw->startdate_year = hdr.startdate_year;
    raw->startdate_month = hdr.startdate_month;
    raw->startdate_day = hdr.startdate_day;
    raw->starttime_hour = hdr.starttime_hour;
    raw->starttime_minute = hdr.starttime_minute;
    raw->starttime_second = hdr.starttime_second;
    raw->starttime_subsecond = hdr.starttime_subsecond;

    // Copy patient information
    raw->patient_name = std::string(hdr.patient_name);
    raw->patient_code = std::string(hdr.patientcode);
    raw->sex = std::string(hdr.sex);
    raw->birthdate = std::string(hdr.birthdate);
    raw->birthdate_year = hdr.birthdate_year;
    raw->birthdate_month = hdr.birthdate_month;
    raw->birthdate_day = hdr.birthdate_day;

    if (verbose)
    {
        std::cout << "Reading " << raw->n_channels << " channels..." << std::endl;
    }

    // Check if there are channels
    if (raw->n_channels == 0)
    {
        std::cerr << "Error: No channel data in file" << std::endl;
        edfclose_file(handle);
        return nullptr;
    }

    // Get number of samples for first channel (assuming all channels have same number of samples, or take maximum)
    raw->n_samples = hdr.signalparam[0].smp_in_file;
    for (int i = 1; i < raw->n_channels; i++)
    {
        if (hdr.signalparam[i].smp_in_file > raw->n_samples)
        {
            raw->n_samples = hdr.signalparam[i].smp_in_file;
        }
    }

    // Pre-allocate space
    raw->data.resize(raw->n_channels);
    raw->ch_names.resize(raw->n_channels);
    raw->sfreq.resize(raw->n_channels);
    raw->ch_types.resize(raw->n_channels);
    raw->units.resize(raw->n_channels);

    // Read metadata and data for each channel
    for (int ch = 0; ch < raw->n_channels; ch++)
    {
        // Copy channel metadata
        raw->ch_names[ch] = std::string(hdr.signalparam[ch].label);
        raw->units[ch] = std::string(hdr.signalparam[ch].physdimension);

        // Calculate sampling rate
        // sf = (smp_in_datarecord * EDFLIB_TIME_DIMENSION) / datarecord_duration
        long long datarecord_duration = hdr.datarecord_duration;
        if (datarecord_duration > 0)
        {
            raw->sfreq[ch] = static_cast<double>(hdr.signalparam[ch].smp_in_datarecord * EDFLIB_TIME_DIMENSION) /
                static_cast<double>(datarecord_duration);
        }
        else
        {
            raw->sfreq[ch] = static_cast<double>(hdr.signalparam[ch].smp_in_datarecord);
        }

        // Infer channel type from channel name (simple heuristic)
        std::string label_lower = raw->ch_names[ch];
        std::transform(label_lower.begin(), label_lower.end(), label_lower.begin(), ::tolower);
        if (label_lower.find("eeg") != std::string::npos || label_lower.find("fp") != std::string::npos ||
            label_lower.find("cz") != std::string::npos || label_lower.find("pz") != std::string::npos)
        {
            raw->ch_types[ch] = "eeg";
        }
        else if (label_lower.find("eog") != std::string::npos)
        {
            raw->ch_types[ch] = "eog";
        }
        else if (label_lower.find("ecg") != std::string::npos)
        {
            raw->ch_types[ch] = "ecg";
        }
        else if (label_lower.find("emg") != std::string::npos)
        {
            raw->ch_types[ch] = "emg";
        }
        else
        {
            raw->ch_types[ch] = "misc"; // Other types
        }

        // Read channel data
        int n_samples_ch = hdr.signalparam[ch].smp_in_file;
        raw->data[ch].resize(n_samples_ch);

        // Reset file pointer to the start position of this channel
        edfseek(handle, ch, 0, EDFSEEK_SET);

        // Read all samples
        int samples_read = edfread_physical_samples(handle, ch, n_samples_ch, raw->data[ch].data());

        if (samples_read < 0)
        {
            std::cerr << "Warning: Error reading channel " << ch << " (" << raw->ch_names[ch] << ")" << std::endl;
        }
        else if (samples_read != n_samples_ch)
        {
            std::cerr << "Warning: Channel " << ch << " (" << raw->ch_names[ch] << ") only read " << samples_read
                      << " samples, expected " << n_samples_ch << std::endl;
        }

        if (verbose && (ch == 0 || (ch + 1) % 10 == 0 || ch == raw->n_channels - 1))
        {
            std::cout << "  Channel " << (ch + 1) << "/" << raw->n_channels << ": " << raw->ch_names[ch] << " ("
                      << samples_read << " samples, " << raw->sfreq[ch] << " Hz)" << std::endl;
        }
    }

    // Read annotations/events
    raw->annotations.resize(hdr.annotations_in_file);
    for (int i = 0; i < hdr.annotations_in_file; i++)
    {
        edf_get_annotation(handle, i, &raw->annotations[i]);
    }

    if (verbose)
    {
        std::cout << "Read " << raw->annotations.size() << " annotations/events" << std::endl;
    }

    // Close file
    edfclose_file(handle);

    if (verbose)
    {
        std::cout << "File reading completed!" << std::endl;
    }

    return raw;
}
