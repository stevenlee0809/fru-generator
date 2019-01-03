#ifndef FRU_H__
#define FRU_H__

#include <stdint.h>

struct fru_bin;

#define OPENBMC_VPD_KEY_CUSTOM_FIELDS_MAX     8


struct chassis_info{
        uint8_t type;

        struct fru_bin *part_number;
        struct fru_bin *serial_number;

        struct fru_bin *custom_field[OPENBMC_VPD_KEY_CUSTOM_FIELDS_MAX];

};


struct board_info{
        uint8_t language_code;

        char time[32];

        struct fru_bin *manufacturer;
        struct fru_bin *product_name;
        struct fru_bin *serial_number;
        struct fru_bin *part_number;
        struct fru_bin *fru_file_id;

        struct fru_bin *custom_field[OPENBMC_VPD_KEY_CUSTOM_FIELDS_MAX];
};

struct product_info{
        uint8_t  language_code;

        struct fru_bin *manufacturer;
        struct fru_bin *product_name;
        struct fru_bin *part_number;
        struct fru_bin *version;
        struct fru_bin *serial_number;
        struct fru_bin *asset_tag;
        struct fru_bin *fru_file_id;

        struct fru_bin *custom_field[OPENBMC_VPD_KEY_CUSTOM_FIELDS_MAX];

};

struct fru_bin *fru_area_field_create_by_string(const char *string);
void fru_bin_release(struct fru_bin *bin);

#endif
