#include "dsp.hpp"
#include "model.hpp"
#include "tensor.hpp"
#include "threaded_inference.hpp"
#include <Eigen/Core>
#include <Eigen/Dense>
#include <cassert>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <libnyquist/Common.h>
#include <libnyquist/Decoders.h>
#include <libnyquist/Encoders.h>
#include <sstream>
#include <string>
#include <thread>
#include <unsupported/Eigen/FFT>
#include <vector>
#include <set>
#include <algorithm>

using namespace demucscpp;
using namespace nqr;

static Eigen::MatrixXf load_audio_file(std::string filename)
{
    // load a wav file with libnyquist
    std::shared_ptr<AudioData> fileData = std::make_shared<AudioData>();

    NyquistIO loader;

    loader.Load(fileData.get(), filename);

    if (fileData->sampleRate != demucscpp::SUPPORTED_SAMPLE_RATE)
    {
        std::stringstream ss;
        ss << "[ERROR] demucs_mt.cpp only supports the following sample rate (Hz): " << SUPPORTED_SAMPLE_RATE
           << ", but input is " << fileData->sampleRate;
        std::cerr << ss.str() << std::endl;
        std::cout << "[CLI_ERROR] " << ss.str() << std::endl;
        exit(1);
    }

    std::cout << "Input samples: "
              << fileData->samples.size() / fileData->channelCount << std::endl;
    std::cout << "Length in seconds: " << fileData->lengthSeconds << std::endl;
    std::cout << "Number of channels: " << fileData->channelCount << std::endl;

    if (fileData->channelCount != 2 && fileData->channelCount != 1)
    {
        std::cerr << "[ERROR] demucs_mt.cpp only supports mono and stereo audio"
                  << std::endl;
        exit(1);
    }

    // number of samples per channel
    size_t N = fileData->samples.size() / fileData->channelCount;

    // create a struct to hold two float vectors for left and right channels
    Eigen::MatrixXf ret(2, N);

    if (fileData->channelCount == 1)
    {
        // Mono case
        for (size_t i = 0; i < N; ++i)
        {
            ret(0, i) = fileData->samples[i]; // left channel
            ret(1, i) = fileData->samples[i]; // right channel
        }
    }
    else
    {
        // Stereo case
        for (size_t i = 0; i < N; ++i)
        {
            ret(0, i) = fileData->samples[2 * i];     // left channel
            ret(1, i) = fileData->samples[2 * i + 1]; // right channel
        }
    }

    return ret;
}

// write a function to write a StereoWaveform to a wav file
static void write_audio_file(const Eigen::MatrixXf &waveform,
                             std::string filename)
{
    // create a struct to hold the audio data
    std::shared_ptr<AudioData> fileData = std::make_shared<AudioData>();

    // set the sample rate
    fileData->sampleRate = SUPPORTED_SAMPLE_RATE;

    // set the number of channels
    fileData->channelCount = 2;

    // set the number of samples
    fileData->samples.resize(waveform.cols() * 2);

    // write the left channel
    for (long int i = 0; i < waveform.cols(); ++i)
    {
        fileData->samples[2 * i] = waveform(0, i);
        fileData->samples[2 * i + 1] = waveform(1, i);
    }

    int encoderStatus =
        encode_wav_to_disk({fileData->channelCount, PCM_FLT, DITHER_TRIANGLE},
                           fileData.get(), filename);
    std::cout << "Encoder Status: " << encoderStatus << std::endl;
}

int main(int argc, const char **argv)
{
    if (argc < 5 || argc > 6)
    {
        std::string usage = std::string("Usage: ") + argv[0] +
                  " <model file> <wav file> <out dir> <num threads> [targets]\n" \
                  "  targets (optional): comma-separated names or indices to export, e.g. 'drums' or '0,3'\n" \
                  "  names: drums,bass,other,vocals[,guitar,piano] depending on model";
        std::cerr << usage << std::endl;
        std::cout << "[CLI_ERROR] " << usage << std::endl;
        exit(1);
    }

    std::cout << "demucs_mt.cpp (Multi-threaded) driver program" << std::endl;

    // load model passed as argument
    std::string model_file = argv[1];

    // load audio passed as argument
    std::string wav_file = argv[2];

    // output dir passed as argument
    std::string out_dir = argv[3];

    // get num threads from user parameter argv[4]
    // cast it to int
    int num_threads = std::stoi(argv[4]);

    // optional 5th arg: targets filter (comma-separated list of names or indices)
    std::set<int> allowedTargets;
    if (argc == 6)
    {
        std::string filt = argv[5];
        // lowercase
        std::transform(filt.begin(), filt.end(), filt.begin(), [](unsigned char c) { return (char)std::tolower(c); });

        auto pushIndex = [&](int idx) {
            if (idx >= 0 && idx <= 5) allowedTargets.insert(idx);
        };

        std::stringstream ss(filt);
        std::string tok;
        while (std::getline(ss, tok, ','))
        {
            // trim spaces
            tok.erase(0, tok.find_first_not_of(" \t\r\n"));
            tok.erase(tok.find_last_not_of(" \t\r\n") + 1);
            if (tok.empty()) continue;

            // numeric index
            if (std::all_of(tok.begin(), tok.end(), ::isdigit))
            {
                pushIndex(std::stoi(tok));
                continue;
            }
            if (tok == "drums") pushIndex(0);
            else if (tok == "bass") pushIndex(1);
            else if (tok == "other") pushIndex(2);
            else if (tok == "vocals") pushIndex(3);
            else if (tok == "guitar") pushIndex(4);
            else if (tok == "piano") pushIndex(5);
        }

        if (!allowedTargets.empty())
        {
            std::cout << "Filtering targets; will export indices: ";
            for (auto i : allowedTargets) std::cout << i << " ";
            std::cout << std::endl;
        }
    }

    // Fallback: allow environment variable SM_DEMUCS_TARGETS when arg not provided
    if (allowedTargets.empty())
    {
        if (const char* envTargets = std::getenv("SM_DEMUCS_TARGETS"))
        {
            std::string filt = envTargets;
            std::transform(filt.begin(), filt.end(), filt.begin(), [](unsigned char c) { return (char)std::tolower(c); });

            auto pushIndex = [&](int idx) {
                if (idx >= 0 && idx <= 5) allowedTargets.insert(idx);
            };

            std::stringstream ss(filt);
            std::string tok;
            while (std::getline(ss, tok, ','))
            {
                tok.erase(0, tok.find_first_not_of(" \t\r\n"));
                tok.erase(tok.find_last_not_of(" \t\r\n") + 1);
                if (tok.empty()) continue;
                if (std::all_of(tok.begin(), tok.end(), ::isdigit)) { pushIndex(std::stoi(tok)); continue; }
                if (tok == "drums") pushIndex(0);
                else if (tok == "bass") pushIndex(1);
                else if (tok == "other") pushIndex(2);
                else if (tok == "vocals") pushIndex(3);
                else if (tok == "guitar") pushIndex(4);
                else if (tok == "piano") pushIndex(5);
            }

            if (!allowedTargets.empty())
            {
                std::cout << "Filtering targets from env; will export indices: ";
                for (auto i : allowedTargets) std::cout << i << " ";
                std::cout << std::endl;
            }
        }
    }

    std::cout << "[CLI] Args OK. model=" << model_file
          << " wav=" << wav_file
          << " out=" << out_dir
          << " threads=" << num_threads;
if (argc == 6) std::cout << " targets=" << argv[5];
std::cout << std::endl;
Eigen::MatrixXf audio = load_audio_file(wav_file);
    Eigen::Tensor3dXf out_targets;

    // initialize a struct demucs_model
    struct demucs_model model
    {
    };

    // debug some members of model
    auto ret = load_demucs_model(model_file, &model);
    std::cout << "demucs_model_load returned " << (ret ? "true" : "false")
              << std::endl;
    if (!ret)
    {
        std::cerr << "Error loading model" << std::endl;
        std::cout << "[CLI_ERROR] Error loading model" << std::endl;
        exit(1);
    }

    int nb_sources = model.is_4sources ? 4 : 6;

    std::cout << "Starting Demucs (" << std::to_string(nb_sources)
              << "-source) inference" << std::endl;

    // create 4 audio matrix same size, to hold output
    Eigen::Tensor3dXf audio_targets =
        demucscppthreaded::threaded_inference(model, audio, num_threads);

    out_targets = audio_targets;

    int nb_out_sources = model.is_4sources ? 4 : 6;

    for (int target = 0; target < nb_out_sources; ++target)
    {
        // filter by optional targets list
        if (!allowedTargets.empty() && allowedTargets.count(target) == 0)
            continue;

        // now write the selected waveforms to files in the output dir
        std::filesystem::path p = out_dir;
        std::filesystem::create_directories(p);

        auto p_target = p / "target_0.wav";

        // target 0,1,2,3 map to drums,bass,other,vocals; 4,5 for 6-source
        std::string target_name;
        switch (target)
        {
        case 0: target_name = "drums"; break;
        case 1: target_name = "bass"; break;
        case 2: target_name = "other"; break;
        case 3: target_name = "vocals"; break;
        case 4: target_name = "guitar"; break;
        case 5: target_name = "piano"; break;
        default:
            std::cerr << "Error: target " << target << " not supported" << std::endl;
            exit(1);
        }

        p_target.replace_filename("target_" + std::to_string(target) + "_" + target_name + ".wav");
        std::cout << "Writing wav file " << p_target << std::endl;

        Eigen::MatrixXf target_waveform(2, audio.cols());
        for (int channel = 0; channel < 2; ++channel)
        {
            for (int sample = 0; sample < audio.cols(); ++sample)
            {
                target_waveform(channel, sample) = out_targets(target, channel, sample);
            }
        }
        write_audio_file(target_waveform, p_target.string());
    }

    return 0;
}
