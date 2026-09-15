#pragma once

#include <string>

namespace renderlab::rdg::exec
{
    // Non-owning physical identity for an imported resource. S5.1 stores an
    // opaque native pointer (tests use a stack dummy; S5.2 stores device
    // objects as void*). The registry copies the token and never frees native.
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
