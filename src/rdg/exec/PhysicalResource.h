#pragma once

#include <string>

namespace renderlab::rdg::exec
{
    // Non-owning physical identity. Imports store a caller pointer (tests use
    // a stack dummy; a device path stores ITexture*/IBuffer*). Internals store
    // an executor-owned CPU stub or device object. The registry copies the
    // token and never frees native.
    struct PhysicalTexture
    {
        void* native = nullptr;
        std::string debugName;
    };

    struct PhysicalBuffer
    {
        void* native = nullptr;
        std::string debugName;
    };
}
