#include "ExecGraph.h"

namespace renderlab::rdg::exec
{
    TextureHandle ExecGraph::CreateTexture(const TextureDesc& desc)
    {
        return m_builder.CreateTexture(desc);
    }

    BufferHandle ExecGraph::CreateBuffer(const BufferDesc& desc)
    {
        return m_builder.CreateBuffer(desc);
    }

    TextureHandle ExecGraph::ImportTexture(const TextureDesc& desc)
    {
        return m_builder.ImportTexture(desc);
    }

    BufferHandle ExecGraph::ImportBuffer(const BufferDesc& desc)
    {
        return m_builder.ImportBuffer(desc);
    }

    void ExecGraph::ExportTexture(TextureHandle handle)
    {
        m_builder.ExportTexture(handle);
    }

    void ExecGraph::ExportBuffer(BufferHandle handle)
    {
        m_builder.ExportBuffer(handle);
    }

    PassBuilder ExecGraph::AddPass(std::string_view name, PassFlags flags, PassLambda lambda)
    {
        const size_t countBefore = m_builder.GetPassCount();
        PassBuilder pass = m_builder.AddPass(name, flags);
        if (!pass.IsValid() || m_builder.GetPassCount() == countBefore)
        {
            return pass;
        }
        if (m_lambdas.size() < m_builder.GetPassCount())
        {
            m_lambdas.resize(m_builder.GetPassCount());
        }
        m_lambdas[pass.PassIndex()] = std::move(lambda);
        return pass;
    }

    const PassLambda* ExecGraph::FindLambda(uint32_t passIndex) const
    {
        if (passIndex >= m_lambdas.size())
        {
            return nullptr;
        }
        if (!m_lambdas[passIndex])
        {
            return nullptr;
        }
        return &m_lambdas[passIndex];
    }
}
