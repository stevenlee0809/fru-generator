#ifndef FRU_H__
#define FRU_H__

#include <stdint.h>
#include <stddef.h>

#define OPENBMC_VPD_KEY_CUSTOM_FIELDS_MAX     8

struct chassis_info{
        uint8_t type;
        const char *part_number;
        const char *serial_number;

        const char *custom_field[OPENBMC_VPD_KEY_CUSTOM_FIELDS_MAX];
};

struct board_info{
        uint8_t language_code;
        const char *mfg_time;
        const char *manufacturer;
        const char *product_name;
        const char *serial_number;
        const char *part_number;
        const char *fru_file_id;

        const char *custom_field[OPENBMC_VPD_KEY_CUSTOM_FIELDS_MAX];
};

struct product_info{
        uint8_t language_code;
        const char *manufacturer;
        const char *product_name;
        const char *part_number;
        const char *version;
        const char *serial_number;
        const char *asset_tag;
        const char *fru_file_id;

        const char *custom_field[OPENBMC_VPD_KEY_CUSTOM_FIELDS_MAX];

};



void fru_bin_generator_by_info(const char *filename,struct chassis_info *chassis_info,
                struct board_info *board_info,struct product_info *product_info);


struct fru_bin;
struct fru_area_chassis_info;
struct fru_area_board_info;
struct fru_area_product_info;

struct fru_bin *fru_bin_create(size_t size);
void fru_bin_release(struct fru_bin *bin);
void fru_bin_debug(struct fru_bin *bin);

struct fru_area_chassis_info *fru_area_chassis_info_create_by_string(struct chassis_info *info);
void fru_area_chassis_info_release(struct fru_area_chassis_info *chassis);

struct fru_area_board_info *fru_area_board_info_create_by_string(struct board_info *info);
void fru_area_board_info_release(struct fru_area_board_info *board);

struct fru_area_product_info *fru_area_product_info_create_by_string(struct product_info *info);
void fru_area_product_info_release(struct fru_area_product_info *product);

void fru_fru_area_chassis_info_append(struct fru_bin *bin,struct fru_area_chassis_info *chassis);
void fru_fru_area_board_info_append(struct fru_bin *bin,struct fru_area_board_info *board);
void fru_fru_area_product_info_append(struct fru_bin *bin,struct fru_area_product_info *product);


void fru_bin_generator_by_bin(const char *filename,struct fru_bin *chassis,
                struct fru_bin *board,struct fru_bin *product);



#endif
