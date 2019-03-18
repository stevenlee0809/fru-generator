#define _XOPEN_SOURCE
#define _BSD_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <endian.h>
#include <time.h>
#include <errno.h>

#include "fru.h"

#define FRU_TYPE_LENGTH_TYPE_CODE_SHIFT 0x06
#define FRU_TYPE_LENGTH_TYPE_CODE_LANGUAGE_CODE 0x03
#define FRU_TYPE_LENGTH_TYPE_CODE_BIN_CODE 0x00

#define FRU_SENTINEL_VALUE 0xC1
#define FRU_FORMAT_VERSION 0x01

#define FRU_COMMON_AREA_MAX_LENGTH 2048 // 256 * 8
#define FRU_COMMON_AREA_LENGTH_OFFSET 0x01
#define FRU_AREA_TYPE_LENGTH_FIELD_MAX 512

struct fru_bin {
	uint8_t *data;
	size_t size;
	size_t length;
};

struct fru_bin *fru_bin_create(size_t size)
{
	struct fru_bin *bin = malloc(sizeof(*bin));
	assert(bin != NULL);
	bin->data = malloc(size);
	assert(bin->data != NULL);
	bin->size = size;
	bin->length = 0;

	return bin;
}

void fru_bin_release(struct fru_bin *bin)
{
	if (bin != NULL) {
		free(bin->data);
		free(bin);
	}
}

static void _fru_bin_expand(struct fru_bin *bin, size_t new_size)
{
	bin->data = realloc(bin->data, new_size);
	assert(bin->data != NULL);
	bin->size = new_size;
}

static void fru_bin_append_byte(struct fru_bin *bin, uint8_t data)
{
	if (bin->length >= bin->size) {
		size_t new_size = bin->size * 2;
		_fru_bin_expand(bin, new_size);
	}

	bin->data[bin->length] = data;
	bin->length++;
}


static void fru_bin_append_bytes(struct fru_bin *bin, const void *data,
				 size_t len)
{
	size_t length_need = bin->length + len;
	if (length_need >= bin->size) {
		size_t new_size = length_need * 2;
		_fru_bin_expand(bin, new_size);
	}

	memcpy(bin->data + bin->length, data, len);
	bin->length += len;
}

static uint8_t crc_calculate(const uint8_t *data, size_t len)
{
	uint8_t sum = 0;
	size_t i;
	for (i = 0; i < len; i++)
		sum += data[i];

	return (-sum);
}

void fru_bin_debug(struct fru_bin *bin)
{
	printf("length=%zu,size=%zu\ndata:", bin->length, bin->size);
	size_t i;
	uint8_t sum = 0;
	for (i = 0; i < bin->length; i++) {
		if (i % 16 == 0)
			printf("\n\t");
		printf("0x%.2x ", bin->data[i]);
		sum += bin->data[i];
	}
	printf("\nsum=0x%.2x\n", sum);
}

static void fru_common_area_init_append(struct fru_bin *bin)
{
	fru_bin_append_byte(bin, FRU_FORMAT_VERSION);
	fru_bin_append_byte(bin, 0);
}


static void fru_common_area_final_append(struct fru_bin *bin)
{
	fru_bin_append_byte(bin, FRU_SENTINEL_VALUE);

	int m = (bin->length + 1) & 7;
	if (m != 0) {
		int remain_length = 8 - m;
		int i;
		for (i = 0; i < remain_length; i++)
			fru_bin_append_byte(bin, 0);
	}

	bin->data[FRU_COMMON_AREA_LENGTH_OFFSET] = (bin->length + 1) >> 3;
	uint8_t crc = crc_calculate(bin->data, bin->length);
	fru_bin_append_byte(bin, crc);
}

static void fru_board_area_append_mfg(struct fru_bin *bin, const char *time)
{
	struct tm tm;
	memset(&tm, 0, sizeof(struct tm));
	strptime(time, "%Y-%m-%d %H:%M:%S", &tm);
	struct tm tm_96;
	memset(&tm_96, 0, sizeof(struct tm));
	tm_96.tm_year = 1996 - 1900;

	time_t sdiff = mktime(&tm) - mktime(&tm_96);
	uint32_t mdiff = htole32(sdiff / 60);

	fru_bin_append_bytes(bin, &mdiff, 3);
}


static uint8_t type_length_code(uint8_t type, size_t length)
{
	length = length & ((1 << FRU_TYPE_LENGTH_TYPE_CODE_SHIFT) - 1);
	type = type << FRU_TYPE_LENGTH_TYPE_CODE_SHIFT;
	return length | type;
}

static void fru_area_field_init_by_string(struct fru_bin *field,
					  const char *string)
{
	field->length = 0;

	uint8_t len = strlen(string);
	uint8_t type_length =
		type_length_code(FRU_TYPE_LENGTH_TYPE_CODE_LANGUAGE_CODE, len);
	fru_bin_append_byte(field, type_length);
	fru_bin_append_bytes(field, string, len);
}

struct fru_bin *fru_area_field_create_by_string(const char *string)
{
	struct fru_bin *field = fru_bin_create(64);
	fru_area_field_init_by_string(field, string);

	return field;
}


static void fru_common_area_field_append(struct fru_bin *bin,
					 struct fru_bin *field)
{
	fru_bin_append_bytes(bin, field->data, field->length);
}

static void fru_common_area_custom_field_append(struct fru_bin *bin,
						struct fru_bin **custom_field)
{
	int i;
	for (i = 0; i < OPENBMC_VPD_KEY_CUSTOM_FIELDS_MAX; i++) {
		if (custom_field[i] == NULL)
			return;
		fru_common_area_field_append(bin, custom_field[i]);
	}
}

struct fru_area_chassis_info {
	uint8_t type;

	struct fru_bin *part_number;
	struct fru_bin *serial_number;

	struct fru_bin *custom_field[OPENBMC_VPD_KEY_CUSTOM_FIELDS_MAX];
};

struct fru_area_board_info {
	uint8_t language_code;

	char mfg_time[32];

	struct fru_bin *manufacturer;
	struct fru_bin *product_name;
	struct fru_bin *serial_number;
	struct fru_bin *part_number;
	struct fru_bin *fru_file_id;

	struct fru_bin *custom_field[OPENBMC_VPD_KEY_CUSTOM_FIELDS_MAX];
};

struct fru_area_product_info {
	uint8_t language_code;

	struct fru_bin *manufacturer;
	struct fru_bin *product_name;
	struct fru_bin *part_number;
	struct fru_bin *version;
	struct fru_bin *serial_number;
	struct fru_bin *asset_tag;
	struct fru_bin *fru_file_id;

	struct fru_bin *custom_field[OPENBMC_VPD_KEY_CUSTOM_FIELDS_MAX];
};


#define FRU_COMMON_AREA_FIELD_APPEND(bin, field)                               \
	do {                                                                   \
		if (field == NULL) {                                           \
			fru_common_area_final_append(bin);                     \
			return;                                                \
		}                                                              \
		fru_common_area_field_append(bin, field);                      \
	} while (0)

void fru_fru_area_chassis_info_append(struct fru_bin *bin,
				      struct fru_area_chassis_info *chassis)
{
	if (chassis == NULL)
		return;

	fru_common_area_init_append(bin);
	fru_bin_append_byte(bin, chassis->type);

	FRU_COMMON_AREA_FIELD_APPEND(bin, chassis->part_number);
	FRU_COMMON_AREA_FIELD_APPEND(bin, chassis->serial_number);

	fru_common_area_custom_field_append(bin, chassis->custom_field);
	fru_common_area_final_append(bin);
}

void fru_fru_area_board_info_append(struct fru_bin *bin,
				    struct fru_area_board_info *board)
{
	if (board == NULL)
		return;

	fru_common_area_init_append(bin);
	fru_bin_append_byte(bin, board->language_code);
	fru_board_area_append_mfg(bin, board->mfg_time);

	FRU_COMMON_AREA_FIELD_APPEND(bin, board->manufacturer);
	FRU_COMMON_AREA_FIELD_APPEND(bin, board->product_name);
	FRU_COMMON_AREA_FIELD_APPEND(bin, board->serial_number);
	FRU_COMMON_AREA_FIELD_APPEND(bin, board->part_number);
	FRU_COMMON_AREA_FIELD_APPEND(bin, board->fru_file_id);

	fru_common_area_custom_field_append(bin, board->custom_field);
	fru_common_area_final_append(bin);
}

void fru_fru_area_product_info_append(struct fru_bin *bin,
				      struct fru_area_product_info *product)
{
	if (product == NULL)
		return;

	fru_common_area_init_append(bin);
	fru_bin_append_byte(bin, product->language_code);

	FRU_COMMON_AREA_FIELD_APPEND(bin, product->manufacturer);
	FRU_COMMON_AREA_FIELD_APPEND(bin, product->product_name);
	FRU_COMMON_AREA_FIELD_APPEND(bin, product->part_number);
	FRU_COMMON_AREA_FIELD_APPEND(bin, product->version);
	FRU_COMMON_AREA_FIELD_APPEND(bin, product->serial_number);
	FRU_COMMON_AREA_FIELD_APPEND(bin, product->asset_tag);
	FRU_COMMON_AREA_FIELD_APPEND(bin, product->fru_file_id);

	fru_common_area_custom_field_append(bin, product->custom_field);
	fru_common_area_final_append(bin);
}

struct fru_common_hdr {
	uint8_t fmtver;
	uint8_t internal;
	uint8_t chassis;
	uint8_t board;
	uint8_t product;
	uint8_t multirec;
	uint8_t pad;
	uint8_t crc;
} __attribute__((packed));


static void _fru_bin_append_header_and_areas(struct fru_bin *bin,
					     struct fru_bin *chassis,
					     struct fru_bin *board,
					     struct fru_bin *product)
{
	assert(bin != NULL && chassis != NULL && board != NULL
	       && product != NULL);

	struct fru_common_hdr hdr;
	hdr.fmtver = FRU_FORMAT_VERSION;
	hdr.internal = 0;
	if (chassis->length == 0)
		hdr.chassis = 0;
	else
		hdr.chassis = sizeof(hdr) >> 3;
	if (board->length == 0)
		hdr.board = 0;
	else
		hdr.board = (sizeof(hdr) + chassis->length) >> 3;
	if (product->length == 0)
		hdr.product = 0;
	else
		hdr.product =
			(sizeof(hdr) + chassis->length + board->length) >> 3;

	hdr.multirec = 0;
	hdr.pad = 0;
	hdr.crc = crc_calculate((uint8_t *)&hdr, sizeof(hdr) - 1);

	fru_bin_append_bytes(bin, &hdr, sizeof(hdr));
	fru_bin_append_bytes(bin, chassis->data, chassis->length);
	fru_bin_append_bytes(bin, board->data, board->length);
	fru_bin_append_bytes(bin, product->data, product->length);
}

static void fru_bin_append_header_and_areas(struct fru_bin *bin,
					    struct fru_bin *chassis,
					    struct fru_bin *board,
					    struct fru_bin *product)
{
	struct fru_bin *chassis_temp = NULL;
	struct fru_bin *board_temp = NULL;
	struct fru_bin *product_temp = NULL;

	if (chassis == NULL)
		chassis = chassis_temp = fru_bin_create(32);
	if (board == NULL)
		board = board_temp = fru_bin_create(32);
	if (product == NULL)
		product = product_temp = fru_bin_create(32);

	_fru_bin_append_header_and_areas(bin, chassis, board, product);

	fru_bin_release(chassis_temp);
	fru_bin_release(board_temp);
	fru_bin_release(product_temp);
}

static void fru_bin_to_file(struct fru_bin *bin, const char *filename)
{
	FILE *fp = fopen(filename, "w+");
	int r = fwrite(bin->data, bin->length, 1, fp);
	if (r != 1) {
		if (ferror(fp))
			fprintf(stderr, "fwrite error %s:%s\n", filename,
				strerror(errno));
		else
			fprintf(stderr, "bin incomplete,just run it again\n");
	}
	fclose(fp);
}


static void custom_field_free(struct fru_bin **custom_field)
{
	int i;
	for (i = 0; i < OPENBMC_VPD_KEY_CUSTOM_FIELDS_MAX; i++)
		fru_bin_release(custom_field[i]);
}

static void custom_field_create_by_string(struct fru_bin **custom_field,
					  const char **string)
{
	int i;
	for (i = 0; i < OPENBMC_VPD_KEY_CUSTOM_FIELDS_MAX; i++) {
		if (string[i] == NULL)
			break;
		custom_field[i] = fru_area_field_create_by_string(string[i]);
	}
}

struct fru_area_chassis_info *
fru_area_chassis_info_create_by_string(struct chassis_info *info)
{
	struct fru_area_chassis_info *chassis = malloc(sizeof(*chassis));
	memset(chassis, 0, sizeof(*chassis));

	chassis->type = info->type;
	chassis->part_number =
		fru_area_field_create_by_string(info->part_number);
	chassis->serial_number =
		fru_area_field_create_by_string(info->serial_number);

	custom_field_create_by_string(chassis->custom_field,
				      info->custom_field);

	return chassis;
}

void fru_area_chassis_info_release(struct fru_area_chassis_info *chassis)
{
	fru_bin_release(chassis->part_number);
	fru_bin_release(chassis->serial_number);
	custom_field_free(chassis->custom_field);
	free(chassis);
}

struct fru_area_board_info *
fru_area_board_info_create_by_string(struct board_info *info)
{
	struct fru_area_board_info *board = malloc(sizeof(*board));
	memset(board, 0, sizeof(*board));

	board->language_code = info->language_code;
	sprintf(board->mfg_time, "%s", info->mfg_time);
	board->manufacturer =
		fru_area_field_create_by_string(info->manufacturer);
	board->product_name =
		fru_area_field_create_by_string(info->product_name);
	board->serial_number =
		fru_area_field_create_by_string(info->serial_number);
	board->part_number = fru_area_field_create_by_string(info->part_number);
	board->fru_file_id = fru_area_field_create_by_string(info->fru_file_id);
	custom_field_create_by_string(board->custom_field, info->custom_field);

	return board;
}

void fru_area_board_info_release(struct fru_area_board_info *board)
{
	fru_bin_release(board->manufacturer);
	fru_bin_release(board->product_name);
	fru_bin_release(board->serial_number);
	fru_bin_release(board->part_number);
	fru_bin_release(board->fru_file_id);
	custom_field_free(board->custom_field);

	free(board);
}

struct fru_area_product_info *
fru_area_product_info_create_by_string(struct product_info *info)
{
	struct fru_area_product_info *product = malloc(sizeof(*product));
	memset(product, 0, sizeof(*product));

	product->language_code = 0;
	product->manufacturer =
		fru_area_field_create_by_string(info->manufacturer);
	product->product_name =
		fru_area_field_create_by_string(info->product_name);
	product->part_number =
		fru_area_field_create_by_string(info->part_number);
	product->version = fru_area_field_create_by_string(info->version);
	product->serial_number =
		fru_area_field_create_by_string(info->serial_number);
	product->asset_tag = fru_area_field_create_by_string(info->asset_tag);
	product->fru_file_id =
		fru_area_field_create_by_string(info->fru_file_id);

	custom_field_create_by_string(product->custom_field,
				      info->custom_field);
	return product;
}

void fru_area_product_info_release(struct fru_area_product_info *product)
{
	fru_bin_release(product->manufacturer);
	fru_bin_release(product->product_name);
	fru_bin_release(product->part_number);
	fru_bin_release(product->version);
	fru_bin_release(product->serial_number);
	fru_bin_release(product->asset_tag);
	fru_bin_release(product->fru_file_id);

	custom_field_free(product->custom_field);
	free(product);
}

void fru_bin_generator_by_bin(const char *filename, struct fru_bin *chassis,
			      struct fru_bin *board, struct fru_bin *product)
{
	struct fru_bin *bin = fru_bin_create(1024);
	fru_bin_append_header_and_areas(bin, chassis, board, product);
	fru_bin_debug(bin);
	fru_bin_to_file(bin, filename);
	fru_bin_release(bin);
}

void fru_bin_generator_by_info(const char *filename,
			       struct chassis_info *chassis_info,
			       struct board_info *board_info,
			       struct product_info *product_info)
{
	struct fru_bin *chassis = fru_bin_create(512);
	if (chassis_info != NULL) {
		struct fru_area_chassis_info *fru_area_chassis_info =
			fru_area_chassis_info_create_by_string(chassis_info);
		fru_fru_area_chassis_info_append(chassis,
						 fru_area_chassis_info);
		fru_bin_debug(chassis);
		fru_area_chassis_info_release(fru_area_chassis_info);
	}

	struct fru_bin *board = fru_bin_create(512);
	if (board_info != NULL) {
		struct fru_area_board_info *fru_area_board_info =
			fru_area_board_info_create_by_string(board_info);
		fru_fru_area_board_info_append(board, fru_area_board_info);
		fru_bin_debug(board);
		fru_area_board_info_release(fru_area_board_info);
	}

	struct fru_bin *product = fru_bin_create(512);
	if (product_info != NULL) {
		struct fru_area_product_info *fru_area_product_info =
			fru_area_product_info_create_by_string(product_info);
		fru_fru_area_product_info_append(product,
						 fru_area_product_info);
		fru_bin_debug(product);
		fru_area_product_info_release(fru_area_product_info);
	}

	struct fru_bin *bin = fru_bin_create(1024);
	_fru_bin_append_header_and_areas(bin, chassis, board, product);
	fru_bin_debug(bin);
	fru_bin_to_file(bin, filename);

	fru_bin_release(board);
	fru_bin_release(product);
	fru_bin_release(bin);
}


#if 0

static struct chassis_info *chassis_info_create()
{
        struct chassis_info *chassis = malloc(sizeof(*chassis));
        memset(chassis,0,sizeof(*chassis));

        chassis->type = 1;
        chassis->part_number = "ON5263m5";
        chassis->serial_number = "OCP";
        chassis->custom_field[0] = "12345678";
         
        return chassis;
}

static void chassis_info_release(struct chassis_info *chassis)
{
        free(chassis);
}

int main()
{
        struct chassis_info *chassis = chassis_info_create();
        fru_bin_generator_by_info("fru.bin",chassis,NULL,NULL);
}


#endif
