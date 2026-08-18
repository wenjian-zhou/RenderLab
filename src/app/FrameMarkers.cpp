#include "FrameMarkers.h"

#include <donut/core/log.h>
#include <nvrhi/d3d12.h>

#include <pix.h>

namespace renderlab::markers
{
    namespace
    {
        ID3D12CommandQueue* GetGraphicsQueue(nvrhi::IDevice* device)
        {
            if (!device)
            {
                return nullptr;
            }
            return device->getNativeObject(nvrhi::ObjectTypes::D3D12_CommandQueue);
        }
    }

    void PrintMarkerNames()
    {
        donut::log::info(
            "Frame markers: %s, %s, %s, %s, %s",
            kFrame,
            kSceneUpdate,
            kRender,
            kUI,
            kPresent);
    }

    CpuMarker::CpuMarker(nvrhi::IDevice* device, const char* name)
        : m_queue(GetGraphicsQueue(device))
    {
        if (m_queue && name)
        {
            PIXBeginEvent(static_cast<ID3D12CommandQueue*>(m_queue), PIX_COLOR_DEFAULT, name);
        }
    }

    CpuMarker::~CpuMarker()
    {
        if (m_queue)
        {
            PIXEndEvent(static_cast<ID3D12CommandQueue*>(m_queue));
        }
    }

    GpuMarker::GpuMarker(nvrhi::ICommandList* commandList, const char* name)
        : m_commandList(commandList)
    {
        if (m_commandList && name)
        {
            m_commandList->beginMarker(name);
        }
    }

    GpuMarker::~GpuMarker()
    {
        if (m_commandList)
        {
            m_commandList->endMarker();
        }
    }

    void EmitStandaloneGpuMarker(
        nvrhi::IDevice* device,
        nvrhi::ICommandList* commandList,
        const char* name)
    {
        if (!device || !commandList || !name)
        {
            return;
        }

        commandList->open();
        {
            GpuMarker marker(commandList, name);
        }
        commandList->close();
        device->executeCommandList(commandList);
    }
}
