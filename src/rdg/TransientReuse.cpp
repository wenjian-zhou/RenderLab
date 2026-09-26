#include "TransientReuse.h"

#include <algorithm>
#include <variant>

namespace renderlab::rdg
{
    namespace
    {
        struct CompatibilityKey
        {
            ResourceKind kind = ResourceKind::Texture;
            Format format = Format::Unknown;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t bytesPerElement = 0;
            uint32_t numElements = 0;

            friend bool operator==(const CompatibilityKey&, const CompatibilityKey&) = default;
        };

        CompatibilityKey MakeKey(const ResourceRecord& record)
        {
            CompatibilityKey key;
            key.kind = record.kind;
            if (record.kind == ResourceKind::Texture)
            {
                const TextureDesc& desc = std::get<TextureDesc>(record.desc);
                key.format = desc.format;
                key.width = desc.width;
                key.height = desc.height;
            }
            else
            {
                const BufferDesc& desc = std::get<BufferDesc>(record.desc);
                key.bytesPerElement = desc.bytesPerElement;
                key.numElements = desc.numElements;
            }
            return key;
        }

        bool IsUnknownTexture(const ResourceRecord& record)
        {
            return record.kind == ResourceKind::Texture &&
                std::get<TextureDesc>(record.desc).format == Format::Unknown;
        }
    }

    TransientReusePlan PlanTransientReuse(
        const GraphBuilder& builder,
        std::span<const ResourceLifetime> lifetimes,
        std::span<const uint32_t> livePassOrder,
        bool reuseEnabled)
    {
        TransientReusePlan plan;
        const uint32_t resourceCount = static_cast<uint32_t>(builder.GetResourceCount());
        plan.physicalOwner.assign(resourceCount, kUnallocated);

        std::vector<const ResourceLifetime*> lifetimeByIndex(resourceCount, nullptr);
        for (const ResourceLifetime& lifetime : lifetimes)
        {
            if (lifetime.resourceIndex < resourceCount)
            {
                lifetimeByIndex[lifetime.resourceIndex] = &lifetime;
            }
        }

        std::vector<uint32_t> slotOfPass(builder.GetPassCount(), kUnallocated);
        for (uint32_t slot = 0; slot < livePassOrder.size(); ++slot)
        {
            const uint32_t passIndex = livePassOrder[slot];
            if (passIndex < slotOfPass.size())
            {
                slotOfPass[passIndex] = slot;
            }
        }

        struct Candidate
        {
            uint32_t resourceIndex = 0;
            uint32_t firstSlot = 0;
            uint32_t lastSlot = 0;
            CompatibilityKey key;
        };
        std::vector<Candidate> candidates;

        for (uint32_t index = 0; index < resourceCount; ++index)
        {
            const ResourceRecord& record = builder.GetResource(index);
            if (record.imported || IsUnknownTexture(record))
            {
                continue;
            }

            const ResourceLifetime* lifetime = lifetimeByIndex[index];
            bool reusable = reuseEnabled && !record.exported && lifetime != nullptr;
            uint32_t firstSlot = 0;
            uint32_t lastSlot = 0;
            if (reusable)
            {
                const bool firstKnown =
                    lifetime->firstPass < slotOfPass.size() && slotOfPass[lifetime->firstPass] != kUnallocated;
                const bool lastKnown =
                    lifetime->lastPass < slotOfPass.size() && slotOfPass[lifetime->lastPass] != kUnallocated;
                if (!firstKnown || !lastKnown)
                {
                    reusable = false;
                }
                else
                {
                    firstSlot = slotOfPass[lifetime->firstPass];
                    lastSlot = slotOfPass[lifetime->lastPass];
                    if (lastSlot < firstSlot)
                    {
                        reusable = false;
                    }
                }
            }

            if (!reusable)
            {
                plan.physicalOwner[index] = index;
                continue;
            }

            candidates.push_back(Candidate{index, firstSlot, lastSlot, MakeKey(record)});
        }

        std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right)
        {
            if (left.firstSlot != right.firstSlot)
            {
                return left.firstSlot < right.firstSlot;
            }
            return left.resourceIndex < right.resourceIndex;
        });

        struct PhysicalSlot
        {
            uint32_t ownerIndex = 0;
            uint32_t occupiedLastSlot = 0;
            CompatibilityKey key;
        };
        std::vector<PhysicalSlot> slots;

        for (const Candidate& candidate : candidates)
        {
            uint32_t bestOwner = kUnallocated;
            size_t bestSlot = slots.size();
            for (size_t slotIndex = 0; slotIndex < slots.size(); ++slotIndex)
            {
                const PhysicalSlot& slot = slots[slotIndex];
                if (slot.key == candidate.key && slot.occupiedLastSlot < candidate.firstSlot)
                {
                    if (bestOwner == kUnallocated || slot.ownerIndex < bestOwner)
                    {
                        bestOwner = slot.ownerIndex;
                        bestSlot = slotIndex;
                    }
                }
            }

            if (bestOwner != kUnallocated)
            {
                slots[bestSlot].occupiedLastSlot = candidate.lastSlot;
                plan.physicalOwner[candidate.resourceIndex] = bestOwner;
                plan.reusePairs.push_back(ReusePair{
                    bestOwner,
                    candidate.resourceIndex,
                    builder.GetResource(bestOwner).name,
                    builder.GetResource(candidate.resourceIndex).name,
                });
            }
            else
            {
                plan.physicalOwner[candidate.resourceIndex] = candidate.resourceIndex;
                slots.push_back(PhysicalSlot{candidate.resourceIndex, candidate.lastSlot, candidate.key});
            }
        }

        std::sort(plan.reusePairs.begin(), plan.reusePairs.end(), [](const ReusePair& left, const ReusePair& right)
        {
            return left.aliasIndex < right.aliasIndex;
        });
        return plan;
    }
}
