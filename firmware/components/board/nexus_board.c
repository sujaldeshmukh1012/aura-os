#include "nexus_board.h"

static const nexus_board_capabilities_t CAPABILITIES = {
    .sku = "LCDWiki ES3C40P",
    .display_width_native = 320,
    .display_height_native = 480,
    .display_width_landscape = 480,
    .display_height_landscape = 320,
    .flash_bytes = 16U * 1024U * 1024U,
    .psram_bytes = 8U * 1024U * 1024U,
    .has_touch = true,
    .has_microphone = true,
    .has_speaker_output = true,
    .has_micro_sd = true,
    .has_native_usb = true,
    .physical_revision_verified = false,
};

const nexus_board_capabilities_t *nexus_board_capabilities(void)
{
    return &CAPABILITIES;
}
