// render — offline CLI for Clear Space.
//
//   render --in <impulse|sine|burst|noise|speechlike|path.wav> [--out out.wav]
//          [--sr 48000] [--block 512] [--seconds 2.0] [--freq 1000] [--level -6]
//          [--seed 1] [--set id=value]... [--stats] [--tail]
//
// Exit codes: 0 ok · 1 usage error · 2 render/IO error · 3 NaN/Inf in output.

#include "Renderer.h"
#include "Sources.h"
#include "Stats.h"
#include "WavIO.h"

#include <juce_events/juce_events.h>

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace
{

using namespace clearspace::render;

struct Options
{
    std::string input;
    std::string output;
    RenderSettings render;
    SourceSpec source;
    bool stats = false;
    bool help = false;
};

void printUsage()
{
    std::cout << "usage: render --in <impulse|sine|burst|noise|speechlike|path.wav> [options]\n"
                 "  --out <file.wav>     write output (32-bit float WAV)\n"
                 "  --sr <hz>            sample rate for synthetic sources / processing (48000)\n"
                 "  --block <n>          block size in samples (512)\n"
                 "  --seconds <s>        synthetic source length (2.0)\n"
                 "  --freq <hz>          sine/burst frequency (1000)\n"
                 "  --level <dbfs>       synthetic source peak level (-6)\n"
                 "  --seed <n>           noise seed (1)\n"
                 "  --set id=value       parameter override (repeatable)\n"
                 "  --stats              print key=value statistics\n"
                 "  --tail               append the processor's reported tail\n"
                 "  --help\n";
}

bool parseArgs(int argc, char** argv, Options& opts, std::string& error)
{
    auto need = [&](int& i, const char* flag) -> const char*
    {
        if (i + 1 >= argc)
        {
            error = std::string(flag) + " needs a value";
            return nullptr;
        }
        return argv[++i];
    };

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const char* v = nullptr;

        if (arg == "--help" || arg == "-h")
            opts.help = true;
        else if (arg == "--stats")
            opts.stats = true;
        else if (arg == "--tail")
            opts.render.appendTail = true;
        else if (arg == "--in")
        {
            if ((v = need(i, "--in")) == nullptr)
                return false;
            opts.input = v;
        }
        else if (arg == "--out")
        {
            if ((v = need(i, "--out")) == nullptr)
                return false;
            opts.output = v;
        }
        else if (arg == "--sr")
        {
            if ((v = need(i, "--sr")) == nullptr)
                return false;
            opts.render.sampleRate = std::stod(v);
        }
        else if (arg == "--block")
        {
            if ((v = need(i, "--block")) == nullptr)
                return false;
            opts.render.blockSize = std::stoi(v);
        }
        else if (arg == "--seconds")
        {
            if ((v = need(i, "--seconds")) == nullptr)
                return false;
            opts.source.seconds = std::stod(v);
        }
        else if (arg == "--freq")
        {
            if ((v = need(i, "--freq")) == nullptr)
                return false;
            opts.source.freqHz = std::stod(v);
        }
        else if (arg == "--level")
        {
            if ((v = need(i, "--level")) == nullptr)
                return false;
            opts.source.levelDb = std::stof(v);
        }
        else if (arg == "--seed")
        {
            if ((v = need(i, "--seed")) == nullptr)
                return false;
            opts.source.seed = static_cast<unsigned>(std::stoul(v));
        }
        else if (arg == "--set")
        {
            if ((v = need(i, "--set")) == nullptr)
                return false;
            const std::string pair = v;
            const auto eq = pair.find('=');
            if (eq == std::string::npos || eq == 0)
            {
                error = "--set expects id=value, got '" + pair + "'";
                return false;
            }
            opts.render.parameterOverrides.emplace_back(pair.substr(0, eq), pair.substr(eq + 1));
        }
        else
        {
            error = "unknown argument '" + arg + "'";
            return false;
        }
    }

    if (!opts.help && opts.input.empty())
    {
        error = "--in is required";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    Options opts;
    std::string error;
    try
    {
        if (!parseArgs(argc, argv, opts, error))
        {
            std::cerr << "render: " << error << "\n\n";
            printUsage();
            return 1;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "render: bad numeric argument (" << e.what() << ")\n";
        return 1;
    }

    if (opts.help)
    {
        printUsage();
        return 0;
    }

    // ---- input ----
    juce::AudioBuffer<float> input;
    if (auto kind = parseSourceKind(opts.input))
    {
        opts.source.kind = *kind;
        input = makeSource(opts.source, opts.render.sampleRate);
    }
    else
    {
        const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile(opts.input);
        auto wav = readWav(file);
        if (!wav)
        {
            std::cerr << "render: cannot read WAV '" << opts.input << "'\n";
            return 2;
        }
        input = std::move(wav->buffer);
        opts.render.sampleRate = wav->sampleRate; // process at the file's rate
    }

    // ---- render ----
    auto out = renderThroughProcessor(input, opts.render);
    if (!out.ok())
    {
        for (const auto& e : out.errors)
            std::cerr << "render: " << e.message << '\n';
        return 2;
    }

    // ---- output ----
    if (!opts.output.empty())
    {
        const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile(opts.output);
        if (!writeWav(file, out.buffer, opts.render.sampleRate))
        {
            std::cerr << "render: cannot write WAV '" << opts.output << "'\n";
            return 2;
        }
    }

    const auto stats = computeStats(out.buffer, opts.render.sampleRate);
    if (opts.stats)
        std::cout << formatStats(stats);

    if (!stats.isClean())
    {
        std::cerr << "render: output contains NaN/Inf (nan=" << stats.totalNanCount()
                  << " inf=" << stats.totalInfCount() << ")\n";
        return 3;
    }
    return 0;
}
