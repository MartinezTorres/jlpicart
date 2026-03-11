// resource_model.cc — ResourceModel implementation.

#include "allocator/resource_model.h"

ResourceModel::ResourceModel() {
    sram_used_    = 0;
    pio_sms_used_ = 0;
    dma_ch_used_  = 0;
}

bool ResourceModel::can_allocate(const ResourceRequirements& req) const {
    return req.sram_bytes   <= sram_available()
        && req.pio_sms      <= pio_sms_available()
        && req.dma_channels <= dma_ch_available();
}

void ResourceModel::apply_allocation(const ResourceRequirements& req) {
    sram_used_    += req.sram_bytes;
    pio_sms_used_ += req.pio_sms;
    dma_ch_used_  += req.dma_channels;
}

void ResourceModel::release(const ResourceRequirements& req) {
    if (req.sram_bytes   <= sram_used_)    sram_used_    -= req.sram_bytes;
    if (req.pio_sms      <= pio_sms_used_) pio_sms_used_ -= req.pio_sms;
    if (req.dma_channels <= dma_ch_used_)  dma_ch_used_  -= req.dma_channels;
}
