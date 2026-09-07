#include "renderer/LightingDebugPass.h"
#include "renderer/PostProcessContract.h"
#include "renderer/PostProcessPass.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace renderlab;

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char* message)
    {
        if (condition)
        {
            std::printf("  PASS  %s\n", message);
            return;
        }
        std::printf("  FAIL  %s\n", message);
        ++g_failures;
    }
}

int RunPostProcessPassTests()
{
    std::printf("RenderLab S3.2 post-process pass tests\n");

    const nvrhi::RasterState postRaster = MakePostProcessRasterState();
    Check(postRaster.cullMode == nvrhi::RasterCullMode::None, "Post-process culls none");
    const nvrhi::DepthStencilState postDepth = MakePostProcessDepthState();
    Check(!postDepth.depthTestEnable && !postDepth.depthWriteEnable,
          "Post-process depth test/write off");

    float exposureEV = 99.f;
    std::string error;
    Check(ParseExposureEv("0", exposureEV, error), "Parse exposure 0");
    Check(exposureEV == 0.f, "Exposure 0 maps to 0 EV");
    Check(ParseExposureEv("-3", exposureEV, error), "Parse exposure -3");
    Check(exposureEV == -3.f, "Exposure -3 maps to -3 EV");
    Check(ParseExposureEv("+2.5", exposureEV, error), "Parse exposure +2.5");
    Check(exposureEV == 2.5f, "Exposure +2.5 maps to 2.5 EV");
    Check(ParseExposureEv("1e1", exposureEV, error), "Parse exposure 1e1");
    Check(exposureEV == 10.f, "Exposure 1e1 maps to 10 EV");
    Check(ParseExposureEv("-0.0", exposureEV, error), "Parse exposure -0.0");
    Check(exposureEV == 0.f, "Exposure -0.0 maps to 0 EV");

    exposureEV = 99.f;
    Check(!ParseExposureEv("", exposureEV, error), "Reject empty exposure");
    Check(!error.empty(), "Empty exposure produces an error string");
    Check(!ParseExposureEv("abc", exposureEV, error), "Reject non-numeric exposure");
    Check(!error.empty(), "Non-numeric exposure produces an error string");
    Check(!ParseExposureEv("2.5x", exposureEV, error), "Reject trailing garbage exposure");
    Check(!ParseExposureEv("nan", exposureEV, error), "Reject nan exposure");
    Check(!ParseExposureEv("inf", exposureEV, error), "Reject inf exposure");
    Check(!ParseExposureEv("-inf", exposureEV, error), "Reject -inf exposure");
    Check(!ParseExposureEv("1e400", exposureEV, error), "Reject overflowing exposure (1e400)");
    Check(exposureEV == 99.f, "Rejected exposures do not modify the output value");
    error.clear();
    Check(ParseExposureEv("15", exposureEV, error), "Exposure +15 parses (UE editor range is a suggestion)");
    Check(exposureEV == 15.f, "Exposure +15 is accepted unclamped");
    Check(ParseExposureEv("-15", exposureEV, error), "Exposure -15 parses");
    Check(exposureEV == -15.f, "Exposure -15 is accepted unclamped");

    const TonemapConstants defaults = MakeDefaultTonemapConstants();
    Check(defaults.exposureEV == kDefaultExposureEV, "Default tonemap constants pin EV 0");
    Check(kDefaultExposureEV == 0.f, "Default exposure EV is 0");

    const TonemapConstants custom = MakeDefaultTonemapConstants();
    nvrhi::ITexture* const dummyHdr = reinterpret_cast<nvrhi::ITexture*>(0x10);
    PostProcessPassInputs inputs = MakePostProcessPassInputs(dummyHdr, custom);
    Check(inputs.hdrSceneColor == dummyHdr, "Post-process inputs carry the HDR texture");
    Check(inputs.tonemapConstants == &custom, "Post-process inputs carry the tonemap constants");

    Check(std::string(kFinalImageFileName) == "final.png", "Final capture file name is final.png");

    return g_failures;
}
