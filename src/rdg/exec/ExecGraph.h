#pragma once

#include "../GraphBuilder.h"
#include "PassContext.h"

#include <functional>
#include <string_view>
#include <vector>

namespace nvrhi
{
    class ICommandList;
}

namespace renderlab::rdg::exec
{
    // Stored at AddPass time and invoked later by GraphExecutor::Execute.
    // The command list is the one Execute selected for this pass. Device-free
    // tests pass nullptr.
    using PassLambda = std::function<void(nvrhi::ICommandList*, PassContext&)>;

    // Logical declarations plus the pass-index lambda table. The logical
    // PassRecord stays name/flags/accesses. This object must outlive any
    // GraphExecutor built from it.
    class ExecGraph
    {
    public:
        GraphBuilder& Builder() { return m_builder; }
        const GraphBuilder& Builder() const { return m_builder; }

        TextureHandle CreateTexture(const TextureDesc& desc);
        BufferHandle CreateBuffer(const BufferDesc& desc);
        TextureHandle ImportTexture(const TextureDesc& desc);
        BufferHandle ImportBuffer(const BufferDesc& desc);
        void ExportTexture(TextureHandle handle);
        void ExportBuffer(BufferHandle handle);

        PassBuilder AddPass(std::string_view name, PassFlags flags, PassLambda lambda);

        const PassLambda* FindLambda(uint32_t passIndex) const;

    private:
        GraphBuilder m_builder;
        std::vector<PassLambda> m_lambdas;
    };
}
