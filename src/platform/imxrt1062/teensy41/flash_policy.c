#if SK_QSPI_FLASH
#include "flash_policy.h"
// PJRC begin() normally formats on mount failure. Never allow that fallback.
int __wrap_lfs_format(lfs_t *fs, const struct lfs_config *config) {
    (void)fs; (void)config; return LFS_ERR_INVAL;
}
int __real_lfs_format(lfs_t *fs, const struct lfs_config *config);
int sk_flash_scan_blank(const struct lfs_config *config, void (*progress)(void)) {
    unsigned char bytes[256];
    if (!config || !config->read || !config->block_count ||
        !config->block_size || config->block_size % sizeof(bytes) ||
        !config->read_size || sizeof(bytes) % config->read_size) return LFS_ERR_INVAL;
    for (lfs_block_t block=0; block<config->block_count; ++block) {
        for (lfs_off_t offset=0; offset<config->block_size; offset+=sizeof(bytes)) {
            int err=config->read(config,block,offset,bytes,sizeof(bytes));
            if (err<0) return err;
            for (unsigned i=0; i<sizeof(bytes); ++i) if (bytes[i]!=0xff) return LFS_ERR_EXIST;
        }
        if (progress) progress();
    }
    return 0;
}
int sk_flash_initialize_blank(lfs_t *fs, const struct lfs_config *config, void (*progress)(void)) {
    int err=sk_flash_scan_blank(config,progress);
    if (err) return err;
    return __real_lfs_format(fs,config);
}
#endif
